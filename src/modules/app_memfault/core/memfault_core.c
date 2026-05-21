/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Memfault core: boot confirm, connectivity state, DNS wait, upload on connect,
 * heartbeat callback, and button-triggered actions (heartbeat, OTA, crash
 * demos).
 */

#include "memfault_core.h"
#include "memfault_log_state_restore.h"
#include "../../messages.h"
#include "../metrics/wifi_metrics.h"
#include "../metrics/stack_metrics.h"

#if CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE
#include "../cdr/nrf70_fw_stats_cdr.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/dfu/mcuboot.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/core/log.h>
#include <memfault/core/trace_event.h>
#include <memfault/panics/coredump.h>
#include <memfault/metrics/connectivity.h>
#include <memfault_ncs.h>

#if CONFIG_MEMFAULT_NCS_STACK_METRICS
#include <memfault_ncs_metrics.h>
#endif

#if defined(CONFIG_POSIX_API)
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#endif

LOG_MODULE_REGISTER(memfault_core, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

#define DNS_CHECK_INTERVAL_SEC 10
#define MEMFAULT_HOSTNAME      "chunks-nrf.memfault.com"
#define DNS_TIMEOUT_SEC        300

static K_SEM_DEFINE(upload_sem, 0, 1);
static volatile bool wifi_connected;

/* Delay log freeze after disconnect so post-disconnect logs are captured */
#define LOG_FREEZE_DELAY_SEC 10

/* Guard: ensures only the first disconnect event schedules the persist work.
 * Cleared when the work fires or when WiFi reconnects.
 */
static bool log_freeze_scheduled;

static void log_freeze_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	log_freeze_scheduled = false;
#if CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE
	int err = memfault_log_state_persist_now();
	if (err) {
		LOG_WRN("Memfault log-state persist failed: %d", err);
	}
#endif
#if CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE
	int cdr_err = mflt_nrf70_fw_stats_cdr_persist_to_flash();
	if (cdr_err) {
		LOG_WRN("Memfault CDR state persist failed: %d", cdr_err);
	}
#endif
}

static K_WORK_DELAYABLE_DEFINE(log_freeze_work, log_freeze_work_fn);

/* Recursive Fibonacci for stack overflow demo */
static int fib(int n)
{
	if (n <= 1) {
		return n;
	}
	return fib(n - 1) + fib(n - 2);
}

/* Memfault SDK callback */
void memfault_metrics_heartbeat_collect_data(void)
{
#if CONFIG_MEMFAULT_NCS_STACK_METRICS
	memfault_ncs_metrics_collect_data();
#endif
	mflt_wifi_metrics_collect();
}

#if defined(CONFIG_POSIX_API)
static bool check_dns_ready(const char *hostname)
{
	int err;
	struct addrinfo *res = NULL;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	err = getaddrinfo(hostname, "443", &hints, &res);
	if (res) {
		freeaddrinfo(res);
	}
	return (err == 0);
}
#else
static bool check_dns_ready(const char *hostname)
{
	ARG_UNUSED(hostname);
	return true;
}
#endif

static void on_connect(void)
{
	if (IS_ENABLED(CONFIG_MEMFAULT_NCS_POST_COREDUMP_ON_NETWORK_CONNECTED) &&
	    memfault_coredump_has_valid_coredump(NULL)) {
		return;
	}

#if CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE
	int restore_err = memfault_log_state_restore_on_connect();
	if (restore_err == 0) {
		/* Trigger collection NOW so the restored ring-buffer content
		 * (which has no saved trigger watermark after a power cycle)
		 * is included in the next upload.
		 */
		memfault_log_trigger_collection();
		LOG_INF("Disconnect-time log state restored — uploading to Memfault");
	}
#endif

#if CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE
	int cdr_restore_err = mflt_nrf70_fw_stats_cdr_restore_from_flash();
	if (cdr_restore_err == 0) {
		LOG_INF("Disconnect-time CDR state restored — uploading to Memfault");
	}
#endif

	memfault_metrics_heartbeat_debug_trigger();
	if (!memfault_packetizer_data_available()) {
		LOG_INF("Memfault: no data queued, skipping upload");
		return;
	}
	LOG_INF("Memfault: uploading queued data...");
	int err = memfault_zephyr_port_post_data();
	if (err) {
		LOG_ERR("Memfault upload failed: %d", err);
	} else {
		LOG_INF("Memfault upload complete");
	}
}

/* Upload thread: wait for connect sem, DNS wait, then on_connect */
static void upload_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		k_sem_take(&upload_sem, K_FOREVER);
		LOG_INF("Connected to network");

		LOG_INF("Waiting for DNS resolver to be ready for Memfault");
		int dns_wait_time = 0;
		while (wifi_connected && !check_dns_ready(MEMFAULT_HOSTNAME)) {
			if (dns_wait_time >= DNS_TIMEOUT_SEC) {
				LOG_ERR("DNS timeout after %d seconds, "
					"continuing anyway",
					DNS_TIMEOUT_SEC);
				break;
			}
			k_sleep(K_SECONDS(DNS_CHECK_INTERVAL_SEC));
			dns_wait_time += DNS_CHECK_INTERVAL_SEC;
		}

		if (!wifi_connected) {
			LOG_WRN("Network disconnected during DNS wait");
			continue;
		}
		on_connect();
	}
}

K_THREAD_DEFINE(memfault_upload_tid, CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE, upload_thread_fn,
		NULL, NULL, NULL, 5, 0, 0);

/* WIFI_CHAN listener */
extern const struct zbus_channel WIFI_CHAN;

static void memfault_wifi_listener(const struct zbus_channel *chan)
{
	const struct wifi_msg *msg = zbus_chan_const_msg(chan);

	switch (msg->type) {
	case WIFI_STA_CONNECTED:
		wifi_connected = true;
		log_freeze_scheduled = false;
		k_work_cancel_delayable(&log_freeze_work);
		memfault_metrics_connectivity_connected_state_change(
			kMemfaultMetricsConnectivityState_Connected);
#if CONFIG_MEMFAULT_NCS_STACK_METRICS
		mflt_stack_metrics_init();
		LOG_INF("Stack metrics monitoring initialized");
#endif
		k_sem_give(&upload_sem);
		break;
	case WIFI_STA_DISCONNECTED:
		wifi_connected = false;
		memfault_metrics_connectivity_connected_state_change(
			kMemfaultMetricsConnectivityState_ConnectionLost);
		if (!log_freeze_scheduled) {
			log_freeze_scheduled = true;
			k_work_schedule(&log_freeze_work, K_SECONDS(LOG_FREEZE_DELAY_SEC));
			LOG_WRN("Network connectivity lost - scheduling Memfault log persist in %d "
				"s",
				LOG_FREEZE_DELAY_SEC);
		}
		break;
	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(memfault_wifi_listener_def, memfault_wifi_listener);
ZBUS_CHAN_ADD_OBS(WIFI_CHAN, memfault_wifi_listener_def, 0);

/* NETWORK_CHAN listener — catches IP-layer loss (DHCP expiry, addr removal)
 * independently of Wi-Fi association state. */
extern const struct zbus_channel NETWORK_CHAN;

static void memfault_network_listener(const struct zbus_channel *chan)
{
	const struct network_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == NETWORK_NOT_READY) {
		if (!log_freeze_scheduled) {
			log_freeze_scheduled = true;
			k_work_schedule(&log_freeze_work, K_SECONDS(LOG_FREEZE_DELAY_SEC));
			LOG_WRN("Network connectivity lost - scheduling Memfault log persist in %d "
				"s",
				LOG_FREEZE_DELAY_SEC);
		}
	}
}

ZBUS_LISTENER_DEFINE(memfault_network_listener_def, memfault_network_listener);
ZBUS_CHAN_ADD_OBS(NETWORK_CHAN, memfault_network_listener_def, 0);

/* BUTTON_CHAN listener: heartbeat, crash demos, metric, trace */
extern const struct zbus_channel BUTTON_CHAN;

static void memfault_button_listener(const struct zbus_channel *chan)
{
	const struct button_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type != BUTTON_RELEASED) {
		return;
	}

	uint8_t btn = msg->button_number;
	uint32_t duration_ms = msg->duration_ms;
	bool long_press = (duration_ms >= BUTTON_LONG_PRESS_THRESHOLD_MS);

	if (btn == 1) {
		if (long_press) {
			LOG_WRN("Stack overflow will now be triggered");
			fib(10000);
		} else {
			LOG_INF("Button 1 short press: Memfault heartbeat + upload");
			if (wifi_connected) {
				memfault_metrics_heartbeat_debug_trigger();
				k_sem_give(&upload_sem);
			} else {
				LOG_WRN("WiFi not connected, cannot collect "
					"metrics");
			}
		}
		return;
	}

	if (btn == 2) {
		if (long_press) {
			volatile uint32_t i;
			LOG_WRN("Division by zero will now be triggered");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
			i = 1 / 0;
#pragma GCC diagnostic pop
			ARG_UNUSED(i);
		}
		/* Button 2 short: OTA check is handled by ota_triggers module
		 */
		return;
	}

	if (btn == 3) {
		int err = MEMFAULT_METRIC_ADD(switch_1_toggle_count, 1);
		if (err) {
			LOG_ERR("Failed to increment switch_1_toggle_count");
		} else {
			LOG_INF("switch_1_toggle_count incremented");
		}
		return;
	}

	if (btn == 4) {
		MEMFAULT_TRACE_EVENT_WITH_LOG(switch_2_toggled, "Switch state: 1");
		LOG_INF("switch_2_toggled event traced");
	}
}

ZBUS_LISTENER_DEFINE(memfault_button_listener_def, memfault_button_listener);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, memfault_button_listener_def, 0);

static int memfault_core_init(void)
{
	LOG_INF("Memfault core init");

#if CONFIG_MEMFAULT_NCS_STACK_METRICS
	/* Register stack monitors at boot so early connect/auth peaks are not missed.
	 * Late-created threads will be retried on Wi-Fi connect.
	 */
	mflt_stack_metrics_init();
#endif

	if (!boot_is_img_confirmed()) {
		int err = boot_write_img_confirmed();
		if (err) {
			LOG_ERR("New OTA FW confirm failed: %d", err);
		} else {
			LOG_INF("New OTA FW confirmed!");
		}
	}

	memfault_log_set_min_save_level(kMemfaultPlatformLogLevel_Debug);
	memfault_metrics_connectivity_connected_state_change(
		kMemfaultMetricsConnectivityState_Started);

	return 0;
}

SYS_INIT(memfault_core_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
