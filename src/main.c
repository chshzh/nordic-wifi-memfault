/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Application entry point. All feature logic runs in modules (button, wifi,
 * memfault, wifi_prov_over_ble, https_client, mqtt_client) via SYS_INIT and
 * zbus.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

LOG_MODULE_REGISTER(memfault_sample, CONFIG_MEMFAULT_SAMPLE_LOG_LEVEL);

int main(void)
{
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *mac_addr =
		iface ? net_if_get_link_addr(iface) : NULL;

	LOG_INF("==============================================");
	LOG_INF("Memfault nRF7002DK");
	LOG_INF("==============================================");
#if defined(CONFIG_MEMFAULT_NCS_FW_VERSION)
	LOG_INF("Version: %s", CONFIG_MEMFAULT_NCS_FW_VERSION);
#endif
	LOG_INF("Build: %s %s", __DATE__, __TIME__);
	LOG_INF("Board: %s", CONFIG_BOARD);
	if (mac_addr && mac_addr->len == 6) {
		LOG_INF("MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac_addr->addr[0],
			mac_addr->addr[1], mac_addr->addr[2], mac_addr->addr[3],
			mac_addr->addr[4], mac_addr->addr[5]);
	}
	LOG_INF("==============================================");
	LOG_INF("All modules run via SYS_INIT and zbus");
	LOG_INF("==============================================");

	for (;;) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
