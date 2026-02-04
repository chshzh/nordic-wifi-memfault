/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/core/log.h>
#include <memfault/core/trace_event.h>
#include <memfault/panics/coredump.h>
#include <memfault/metrics/connectivity.h>
#include <memfault_ncs.h>
#include <dk_buttons_and_leds.h>
#include "mflt_wifi_metrics.h"
#include <zephyr/sys/util.h>
#include <zephyr/dfu/mcuboot.h>

#if defined(CONFIG_POSIX_API)
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#endif

#include "mflt_ota_triggers.h"

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#if CONFIG_MEMFAULT_NCS_STACK_METRICS
#include <memfault_ncs_metrics.h>
#include "mflt_stack_metrics.h"
#endif

#ifdef CONFIG_BLE_PROV_ENABLED
#include "ble_provisioning.h"
#endif

#ifdef CONFIG_HTTPS_CLIENT_ENABLED
#include "https_client.h"
#endif

#ifdef CONFIG_MQTT_CLIENT_ENABLED
#include "mqtt_client.h"
#endif

#ifdef CONFIG_NRF70_FW_STATS_CDR_ENABLED
#include "mflt_nrf70_fw_stats_cdr.h"
#endif

LOG_MODULE_REGISTER(memfault_sample, CONFIG_MEMFAULT_SAMPLE_LOG_LEVEL);

/* Macros used to subscribe to specific Zephyr NET management events. */
#define L4_EVENT_MASK         (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define DNS_CHECK_INTERVAL_SEC 10
#define MEMFAULT_HOSTNAME      "chunks-nrf.memfault.com"

#define LONG_PRESS_THRESHOLD_MS 3000

static K_SEM_DEFINE(net_conn_sem, 0, 1);
static bool wifi_connected = false;
static void reconnect_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(reconnect_work, reconnect_work_handler);

static int64_t button_press_ts_btn1;
static int64_t button_press_ts_btn2;

/* Zephyr NET management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

/* Check if DNS is ready by attempting to resolve a hostname */
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

/* Recursive Fibonacci calculation used to trigger stack overflow. */
static int fib(int n)
{
	if (n <= 1) {
		return n;
	}

	return fib(n - 1) + fib(n - 2);
}

void memfault_metrics_heartbeat_collect_data(void)
{
#if CONFIG_MEMFAULT_NCS_STACK_METRICS
	/* Maintain default NCS metrics collection (stack, connectivity, etc.)
	 */
	memfault_ncs_metrics_collect_data();
#endif

	/* Append custom Wi-Fi metrics */
	mflt_wifi_metrics_collect();
}

/* Handle button presses and trigger faults that can be captured and sent to
 * the Memfault cloud for inspection after rebooting:
 * Only button 1 is available on Thingy:91, the rest are available on nRF9160
 *DK. Button 1: Trigger stack overflow. Button 2: Trigger NULL-pointer
 *dereference.
 */
static void button_handler(uint32_t button_states, uint32_t has_changed)
{
	uint32_t buttons_pressed = has_changed & button_states;
	uint32_t buttons_released = has_changed & ~button_states;

	if (buttons_pressed & DK_BTN1_MSK) {
		button_press_ts_btn1 = k_uptime_get();
	}

	if (buttons_released & DK_BTN1_MSK) {
		int64_t duration = k_uptime_get() - button_press_ts_btn1;
		if (duration >= LONG_PRESS_THRESHOLD_MS) {
			LOG_WRN("Stack overflow will now be triggered");
			fib(10000);
		} else {
			LOG_INF("Button 1 short press detected, triggering "
				"Memfault heartbeat");
			if (wifi_connected) {
				memfault_metrics_heartbeat_debug_trigger();
#ifdef CONFIG_NRF70_FW_STATS_CDR_ENABLED
				int cdr_err = mflt_nrf70_fw_stats_cdr_collect();
				if (cdr_err) {
					LOG_WRN("nRF70 FW stats CDR collection "
						"failed: %d",
						cdr_err);
				} else {
					LOG_INF("nRF70 FW stats CDR collected "
						"(%zu bytes), "
						"uploading...",
						mflt_nrf70_fw_stats_cdr_get_size());
				}
#endif
				memfault_zephyr_port_post_data();
			} else {
				LOG_WRN("WiFi not connected, cannot collect "
					"metrics");
			}
		}
	}

	if (buttons_pressed & DK_BTN2_MSK) {
		button_press_ts_btn2 = k_uptime_get();
	}

	if (buttons_released & DK_BTN2_MSK) {
		int64_t duration = k_uptime_get() - button_press_ts_btn2;
		if (duration >= LONG_PRESS_THRESHOLD_MS) {
			volatile uint32_t i;

			LOG_WRN("Division by zero will now be triggered");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
			i = 1 / 0;
#pragma GCC diagnostic pop
			ARG_UNUSED(i);
		} else {
			LOG_INF("Button 2 short press detected, scheduling "
				"Memfault OTA check");
			mflt_ota_triggers_notify_button();
		}
	}

	if (buttons_pressed & DK_BTN3_MSK) {
		/* DK_BTN3_MSK is Switch 1 on nRF9160 DK. */
		int err = MEMFAULT_METRIC_ADD(switch_1_toggle_count, 1);
		if (err) {
			LOG_ERR("Failed to increment switch_1_toggle_count");
		} else {
			LOG_INF("switch_1_toggle_count incremented");
		}
	}

	if (buttons_pressed & DK_BTN4_MSK) {
		/* DK_BTN4_MSK is Switch 2 on nRF9160 DK. */
		MEMFAULT_TRACE_EVENT_WITH_LOG(
			switch_2_toggled, "Switch state: %d",
			buttons_pressed & DK_BTN4_MSK ? 1 : 0);
		LOG_INF("switch_2_toggled event has been traced, button state: "
			"%d",
			buttons_pressed & DK_BTN4_MSK ? 1 : 0);
	}
}

static void on_connect(void)
{
#if IS_ENABLED(MEMFAULT_NCS_LTE_METRICS)
	uint32_t time_to_lte_connection;

	/* Retrieve the LTE time to connect metric. */
	memfault_metrics_heartbeat_timer_read(
		MEMFAULT_METRICS_KEY(ncs_lte_time_to_connect_ms),
		&time_to_lte_connection);

	LOG_INF("Time to connect: %d ms", time_to_lte_connection);
#endif /* IS_ENABLED(MEMFAULT_NCS_LTE_METRICS) */

	if (IS_ENABLED(
		    CONFIG_MEMFAULT_NCS_POST_COREDUMP_ON_NETWORK_CONNECTED) &&
	    memfault_coredump_has_valid_coredump(NULL)) {
		/* Coredump sending handled internally */
		return;
	}

	LOG_INF("Sending already captured data to Memfault");

	/* Trigger collection of heartbeat data. */
	memfault_metrics_heartbeat_debug_trigger();

	/* Flush the log ring buffer into the Memfault data recorder. */
	// memfault_log_trigger_collection();
	/* Check if there is any data available to be sent. */
	if (!memfault_packetizer_data_available()) {
		LOG_DBG("There was no data to be sent");
		return;
	}

	LOG_DBG("Sending stored data...");

	/* Send the data that has been captured to the memfault cloud.
	 * This will also happen periodically, with an interval that can be
	 * configured using CONFIG_MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS.
	 */
	memfault_zephyr_port_post_data();
}

static void l4_event_handler(struct net_mgmt_event_callback *cb,
			     uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_L4_CONNECTED:
		LOG_INF("Network connectivity established");
		wifi_connected = true;

		/* Signal connectivity state change to Memfault */
		memfault_metrics_connectivity_connected_state_change(
			kMemfaultMetricsConnectivityState_Connected);

		/* Initialize stack metrics monitoring */
#if CONFIG_MEMFAULT_NCS_STACK_METRICS
		mflt_stack_metrics_init();
		LOG_INF("Stack metrics monitoring initialized");
#endif

		/* Update BLE advertisement with WiFi connected status */
#ifdef CONFIG_BLE_PROV_ENABLED
		ble_prov_update_wifi_status(true);
#endif

		/* Notify HTTPS client that network is connected */
#ifdef CONFIG_HTTPS_CLIENT_ENABLED
		https_client_notify_connected();
#endif

		/* Notify MQTT client that network is connected */
#ifdef CONFIG_MQTT_CLIENT_ENABLED
		app_mqtt_client_notify_connected();
#endif

		k_sem_give(&net_conn_sem);
		mflt_ota_triggers_notify_connected();
		break;
	case NET_EVENT_L4_DISCONNECTED:
		LOG_INF("Network connectivity lost");
		wifi_connected = false;

		/* Signal connectivity state change to Memfault */
		memfault_metrics_connectivity_connected_state_change(
			kMemfaultMetricsConnectivityState_ConnectionLost);

		/* Update BLE advertisement with WiFi disconnected status */
#ifdef CONFIG_BLE_PROV_ENABLED
		ble_prov_update_wifi_status(false);
#endif

		/* Notify HTTPS client that network is disconnected */
#ifdef CONFIG_HTTPS_CLIENT_ENABLED
		https_client_notify_disconnected();
#endif

		/* Notify MQTT client that network is disconnected */
#ifdef CONFIG_MQTT_CLIENT_ENABLED
		app_mqtt_client_notify_disconnected();
#endif
		break;
	default:
		LOG_DBG("Unknown event: 0x%016llX", mgmt_event);
		return;
	}
}

static void connectivity_event_handler(struct net_mgmt_event_callback *cb,
				       uint64_t mgmt_event,
				       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_CONN_IF_FATAL_ERROR:
		LOG_ERR("Connectivity fatal error, scheduling reconnect");
		wifi_connected = false;
#ifdef CONFIG_BLE_PROV_ENABLED
		ble_prov_update_wifi_status(false);
#endif
		k_work_reschedule(&reconnect_work, K_SECONDS(2));
		break;
	case NET_EVENT_CONN_IF_TIMEOUT:
		LOG_WRN("Connectivity timeout, scheduling reconnect");
		k_work_reschedule(&reconnect_work, K_SECONDS(1));
		break;
	default:
		break;
	}
}

static void reconnect_work_handler(struct k_work *work)
{
	int err;

	LOG_INF("Retrying network bring-up after connectivity fault");
	err = conn_mgr_all_if_up(true);
	if (err) {
		LOG_ERR("conn_mgr_all_if_up during retry failed: %d", err);
		return;
	}

	err = conn_mgr_all_if_connect(true);
	if (err) {
		LOG_ERR("conn_mgr_all_if_connect during retry failed: %d", err);
	}
}

int main(void)
{
	int err;

	LOG_INF("Memfault sample has started! Version: %s",
		CONFIG_MEMFAULT_NCS_FW_VERSION);

	if (!boot_is_img_confirmed()) {
		err = boot_write_img_confirmed();
		if (err) {
			LOG_ERR("New OTA FW confirm failed: %d, rollback to "
				"previous version",
				err);
		} else {
			LOG_INF("New OTA FW confirmed!");
		}
	}

	/* Lower the Memfault log capture threshold so debug logs are stored &
	 * uploaded, which need set CONFIG_MEMFAULT_LOGGING_RAM_SIZE bigger
	 * enough*/
	memfault_log_set_min_save_level(kMemfaultPlatformLogLevel_Debug);

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("dk_buttons_init, error: %d", err);
	}

	/* Setup handler for Zephyr NET Connection Manager events. */
	net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
	net_mgmt_add_event_callback(&l4_cb);

	/* Setup handler for Zephyr NET Connection Manager Connectivity layer.
	 */
	net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler,
				     CONN_LAYER_EVENT_MASK);
	net_mgmt_add_event_callback(&conn_cb);

	/* Signal to Memfault that we're starting connectivity (attempting to
	 * connect) */
	memfault_metrics_connectivity_connected_state_change(
		kMemfaultMetricsConnectivityState_Started);

#ifdef CONFIG_HTTPS_CLIENT_ENABLED
	/* Initialize HTTPS client */
	err = https_client_init();
	if (err) {
		LOG_ERR("HTTPS client initialization failed: %d", err);
	}
#endif

#ifdef CONFIG_MQTT_CLIENT_ENABLED
	/* Initialize MQTT client */
	err = app_mqtt_client_init();
	if (err) {
		LOG_ERR("MQTT client initialization failed: %d", err);
	}
#endif

#ifdef CONFIG_NRF70_FW_STATS_CDR_ENABLED
	/* Initialize nRF70 FW stats CDR module */
	err = mflt_nrf70_fw_stats_cdr_init();
	if (err) {
		LOG_ERR("nRF70 FW stats CDR initialization failed: %d", err);
	} else {
		LOG_INF("nRF70 FW stats CDR module initialized");
	}
#endif

#ifdef CONFIG_BLE_PROV_ENABLED
	/* Sleep 1 second to allow initialization of wifi driver. */
	k_sleep(K_SECONDS(1));

	/* Bring up network interface first */
	err = conn_mgr_all_if_up(true);
	if (err) {
		LOG_ERR("conn_mgr_all_if_up, error: %d", err);
	}

	/* Initialize BLE provisioning so re-provisioning is always available */
	err = ble_prov_init();
	if (err) {
		LOG_ERR("BLE provisioning initialization failed: %d", err);
	} else {
		LOG_INF("BLE provisioning initialized successfully");
	}

	/* Check if WiFi credentials are stored before attempting connection */
	if (wifi_credentials_is_empty()) {
		LOG_INF("No stored WiFi credentials found");
		LOG_INF("Please provision WiFi credentials using BLE");
	} else {
		LOG_INF("Attempting to connect to stored WiFi network");

		/* Try to connect using stored WiFi credentials */
		err = conn_mgr_all_if_connect(true);
		if (err) {
			LOG_ERR("WiFi connection request failed: %d", err);
		}
	}
#else
	/* Connecting to the configured connectivity layer.
	 * Wi-Fi or LTE depending on the board that the sample was built for.
	 */
	LOG_INF("Bringing network interface up and connecting to the network");

	err = conn_mgr_all_if_connect(true);
	if (err) {
		__ASSERT(false, "conn_mgr_all_if_connect, error: %d", err);
		return err;
	}
#endif

	/* Performing in an infinite loop to be resilient against
	 * re-connect bursts directly after boot, e.g. when connected
	 * to a roaming network or via weak signal. Note that
	 * Memfault data will be uploaded periodically every
	 * CONFIG_MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS.
	 * We post data here so as soon as a connection is available
	 * the latest data will be pushed to Memfault.
	 */

	while (1) {
		k_sem_take(&net_conn_sem, K_FOREVER);
		LOG_INF("Connected to network");

		/* Wait for DNS resolver to be ready before triggering Memfault
		 * uploads */
		LOG_INF("Waiting for DNS resolver to be ready for Memfault");
		int dns_wait_time = 0;
		const int dns_timeout = 300; /* 5 minutes */
		while (wifi_connected && !check_dns_ready(MEMFAULT_HOSTNAME)) {
			if (dns_wait_time >= dns_timeout) {
				LOG_ERR("DNS timeout after %d seconds for %s, "
					"continuing anyway",
					dns_timeout, MEMFAULT_HOSTNAME);
				break;
			}
			LOG_INF("DNS not ready for %s, checking again in %d "
				"seconds",
				MEMFAULT_HOSTNAME, DNS_CHECK_INTERVAL_SEC);
			k_sleep(K_SECONDS(DNS_CHECK_INTERVAL_SEC));
			dns_wait_time += DNS_CHECK_INTERVAL_SEC;
		}

		if (!wifi_connected) {
			LOG_WRN("Network disconnected during DNS wait, "
				"aborting Memfault upload");
			continue;
		}

		if (dns_wait_time < dns_timeout) {
			LOG_INF("DNS ready for %s after %d seconds, triggering "
				"Memfault data upload",
				MEMFAULT_HOSTNAME, dns_wait_time);
		} else {
			LOG_WRN("Attempting Memfault upload without DNS "
				"confirmation");
		}
		on_connect();
	}
}
