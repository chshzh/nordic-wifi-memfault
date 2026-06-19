/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Memfault heap and thread stack metric updates driven by zego/memonitor.
 *
 * Registers a Zbus listener on MEMONITOR_CHAN (runs in memonitor's sysworkq
 * context — no extra thread).  On each sample:
 *   - Reads all k_heap snapshots → updates ncs_system_heap_* and ncs_mbedtls_heap_*
 *     Memfault metric keys (same keys as the removed heap_monitor module).
 *   - Reads all thread snapshots → matches thread names against a compile-time
 *     lookup table → updates per-thread _unused_stack Memfault metric keys.
 *
 * Flash notes (nRF7002DK is flash-constrained at ~99%):
 *   - ZBUS_LISTENER runs in the publisher's context: no dedicated thread stack.
 *   - Heap/thread buffers are file-scope static (BSS, not stack).
 *   - The thread lookup table is an array of const char* + MemfaultMetricId;
 *     string literals are shared with other TUs by the linker.
 */

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <memfault/metrics/metrics.h>
#include <memonitor.h>

LOG_MODULE_DECLARE(app_memfault, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

/* ── Thread lookup table ────────────────────────────────────────────────────
 * Maps Zephyr thread names to Memfault heartbeat metric keys.
 * Metric keys must be defined in memfault_metrics_heartbeat_config.def.
 * Threads not present in the table are silently skipped.
 */
struct thread_metric {
	const char *name;
	MemfaultMetricId key;
};

static const struct thread_metric s_thread_table[] = {
	/* Wi-Fi / WPA supplicant threads */
	{"hostap_iface_wq", MEMFAULT_METRICS_KEY(ncs_wifi_hostap_iface_unused_stack)},
	{"hostap_handler", MEMFAULT_METRICS_KEY(ncs_wifi_hostap_handler_unused_stack)},
	{"nrf70_intr_wq", MEMFAULT_METRICS_KEY(ncs_wifi_intr_unused_stack)},
	{"nrf70_bh_wq", MEMFAULT_METRICS_KEY(ncs_wifi_bh_unused_stack)},
	/* Memfault internal threads */
	{"mflt_http", MEMFAULT_METRICS_KEY(ncs_mflt_http_unused_stack)},
	{"memfault_upload_tid", MEMFAULT_METRICS_KEY(memfault_upload_unused_stack)},
	{"mflt_ota_triggers_tid", MEMFAULT_METRICS_KEY(mflt_ota_triggers_unused_stack)},
	/* Network threads */
	{"mqtt_helper_thread", MEMFAULT_METRICS_KEY(ncs_mqtt_helper_unused_stack)},
	{"conn_mgr_monitor", MEMFAULT_METRICS_KEY(ncs_conn_mgr_monitor_unused_stack)},
	{"net_socket_service", MEMFAULT_METRICS_KEY(ncs_net_socket_service_unused_stack)},
	{"rx_q[0]", MEMFAULT_METRICS_KEY(ncs_rx_q0_unused_stack)},
	{"tx_q[0]", MEMFAULT_METRICS_KEY(ncs_tx_q0_unused_stack)},
	{"net_mgmt", MEMFAULT_METRICS_KEY(ncs_net_mgmt_unused_stack)},
	{"tcp_work", MEMFAULT_METRICS_KEY(ncs_tcp_work_unused_stack)},
	/* System threads */
	{"shell_uart", MEMFAULT_METRICS_KEY(ncs_shell_uart_unused_stack)},
	{"logging", MEMFAULT_METRICS_KEY(ncs_logging_unused_stack)},
	{"main", MEMFAULT_METRICS_KEY(ncs_main_unused_stack)},
	{"sysworkq", MEMFAULT_METRICS_KEY(ncs_sysworkq_unused_stack)},
	{"k_system_thread", MEMFAULT_METRICS_KEY(ncs_system_thread_unused_stack)},
#if CONFIG_APP_HTTPS_CLIENT_MODULE
	{"app_https_client_tid", MEMFAULT_METRICS_KEY(app_https_client_unused_stack)},
#endif
#if CONFIG_APP_MQTT_CLIENT_MODULE
	{"app_mqtt_client_tid", MEMFAULT_METRICS_KEY(app_mqtt_client_unused_stack)},
#endif
#if CONFIG_BT
	{"bt_rx", MEMFAULT_METRICS_KEY(ncs_bt_rx_unused_stack)},
	{"bt_tx", MEMFAULT_METRICS_KEY(ncs_bt_tx_unused_stack)},
	{"bt_hci_isr", MEMFAULT_METRICS_KEY(ncs_bt_hci_isr_unused_stack)},
	{"bt_host_tx", MEMFAULT_METRICS_KEY(ncs_bt_host_tx_unused_stack)},
#endif
#if CONFIG_NTP_MODULE
	{"ntp", MEMFAULT_METRICS_KEY(ncs_ntp_unused_stack)},
#endif
};

/* ── Static snapshot buffers ────────────────────────────────────────────────
 * File-scope static: never on any stack; safe because ZBUS_LISTENER callbacks
 * are dispatched serially from the publisher (memonitor's sysworkq work item,
 * which is a k_work_delayable — never re-entrant).
 */
static struct memonitor_heap_entry s_heaps[MEMONITOR_MAX_HEAPS];
static struct memonitor_thread_entry s_threads[MEMONITOR_MAX_THREADS];

/* ── Heap metric update ─────────────────────────────────────────────────────*/

static void update_heap_metrics(void)
{
#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
	uint8_t count = 0;

	if (memonitor_get_heaps(s_heaps, ARRAY_SIZE(s_heaps), &count) != 0) {
		return;
	}

	for (uint8_t i = 0; i < count; i++) {
#if CONFIG_HEAP_MEM_POOL_SIZE > 0
		if (strcmp(s_heaps[i].name, "_system_heap") == 0) {
			MEMFAULT_METRIC_SET_UNSIGNED(ncs_system_heap_total,
						     (uint32_t)s_heaps[i].total);
			MEMFAULT_METRIC_SET_UNSIGNED(ncs_system_heap_used,
						     (uint32_t)s_heaps[i].used);
			MEMFAULT_METRIC_SET_UNSIGNED(ncs_system_heap_peak,
						     (uint32_t)s_heaps[i].watermark);
			continue;
		}
#endif
#if CONFIG_MBEDTLS_ENABLE_HEAP
		if (strcmp(s_heaps[i].name, "mbedtls_heap") == 0) {
			MEMFAULT_METRIC_SET_UNSIGNED(ncs_mbedtls_heap_total,
						     (uint32_t)s_heaps[i].total);
			MEMFAULT_METRIC_SET_UNSIGNED(ncs_mbedtls_heap_used,
						     (uint32_t)s_heaps[i].used);
			MEMFAULT_METRIC_SET_UNSIGNED(ncs_mbedtls_heap_peak,
						     (uint32_t)s_heaps[i].watermark);
			continue;
		}
#endif
	}
#endif /* ZEGO_MEMONITOR_HEAP_MONITOR */
}

/* ── Thread metric update ───────────────────────────────────────────────────*/

static void update_thread_metrics(void)
{
#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
	uint8_t count = 0;

	if (memonitor_get_threads(s_threads, ARRAY_SIZE(s_threads), &count) != 0) {
		return;
	}

	for (uint8_t i = 0; i < count; i++) {
		if (s_threads[i].stack_size == 0) {
			continue;
		}
		const size_t unused = s_threads[i].stack_size - s_threads[i].stack_hwm;

		for (size_t j = 0; j < ARRAY_SIZE(s_thread_table); j++) {
			if (strcmp(s_threads[i].name, s_thread_table[j].name) == 0) {
				memfault_metrics_heartbeat_set_unsigned(s_thread_table[j].key,
									(uint32_t)unused);
				break;
			}
		}
	}
#endif /* ZEGO_MEMONITOR_THREAD_MONITOR */
}

/* ── Zbus listener ──────────────────────────────────────────────────────────*/

static void memonitor_metrics_cb(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan);
	update_heap_metrics();
	update_thread_metrics();
}

ZBUS_LISTENER_DEFINE(memonitor_metrics_listener, memonitor_metrics_cb);
ZBUS_CHAN_ADD_OBS(MEMONITOR_CHAN, memonitor_metrics_listener, 0);
