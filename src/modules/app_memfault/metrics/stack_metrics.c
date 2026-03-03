/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "stack_metrics.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <memfault/metrics/metrics.h>
#include <memfault_ncs_metrics.h>

LOG_MODULE_REGISTER(stack_metrics, CONFIG_MEMFAULT_MODULE_LOG_LEVEL);

#if CONFIG_MEMFAULT_NCS_STACK_METRICS
static struct memfault_ncs_metrics_thread stack_metrics_threads[] = {
	{.thread_name = "hostap_iface_wq",
	 .key = MEMFAULT_METRICS_KEY(ncs_wifi_hostap_iface_unused_stack)},
	{.thread_name = "hostap_handler",
	 .key = MEMFAULT_METRICS_KEY(ncs_wifi_hostap_handler_unused_stack)},
	{.thread_name = "nrf70_intr_wq", .key = MEMFAULT_METRICS_KEY(ncs_wifi_intr_unused_stack)},
	{.thread_name = "nrf70_bh_wq", .key = MEMFAULT_METRICS_KEY(ncs_wifi_bh_unused_stack)},
	{.thread_name = "mflt_http", .key = MEMFAULT_METRICS_KEY(ncs_mflt_http_unused_stack)},
	{.thread_name = "conn_mgr_monitor",
	 .key = MEMFAULT_METRICS_KEY(ncs_conn_mgr_monitor_unused_stack)},
	{.thread_name = "net_socket_service",
	 .key = MEMFAULT_METRICS_KEY(ncs_net_socket_service_unused_stack)},
	{.thread_name = "rx_q[0]", .key = MEMFAULT_METRICS_KEY(ncs_rx_q0_unused_stack)},
	{.thread_name = "tx_q[0]", .key = MEMFAULT_METRICS_KEY(ncs_tx_q0_unused_stack)},
	{.thread_name = "net_mgmt", .key = MEMFAULT_METRICS_KEY(ncs_net_mgmt_unused_stack)},
	{.thread_name = "tcp_work", .key = MEMFAULT_METRICS_KEY(ncs_tcp_work_unused_stack)},
	{.thread_name = "memfault_upload_tid", .key = MEMFAULT_METRICS_KEY(memfault_upload_unused_stack)},
	{.thread_name = "mflt_ota_triggers_tid", .key = MEMFAULT_METRICS_KEY(mflt_ota_triggers_unused_stack)},
#if CONFIG_APP_HTTPS_CLIENT_ENABLED
	{.thread_name = "app_https_client_tid", .key = MEMFAULT_METRICS_KEY(app_https_client_unused_stack)},
#endif
#if CONFIG_APP_MQTT_CLIENT_ENABLED
	{.thread_name = "app_mqtt_client_tid", .key = MEMFAULT_METRICS_KEY(app_mqtt_client_unused_stack)},
#endif
	{.thread_name = "shell_uart", .key = MEMFAULT_METRICS_KEY(ncs_shell_uart_unused_stack)},
	{.thread_name = "logging", .key = MEMFAULT_METRICS_KEY(ncs_logging_unused_stack)},
	{.thread_name = "main", .key = MEMFAULT_METRICS_KEY(ncs_main_unused_stack)}
};

void mflt_stack_metrics_init(void)
{
	int err;

	LOG_INF("Initializing stack metrics monitoring for %zu threads",
		ARRAY_SIZE(stack_metrics_threads));

	for (size_t i = 0; i < ARRAY_SIZE(stack_metrics_threads); i++) {
		err = memfault_ncs_metrics_thread_add(&stack_metrics_threads[i]);
		if (err) {
			LOG_ERR("Failed to add thread: %s for stack monitoring, err: %d",
				stack_metrics_threads[i].thread_name, err);
		} else {
			LOG_INF("Successfully added thread: %s for stack monitoring",
				stack_metrics_threads[i].thread_name);
		}
	}
}
#endif
