/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Application entry point. This file is intentionally neutral — all feature
 * logic lives in modules under src/modules/ and starts via SYS_INIT / zbus.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

int main(void)
{
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *mac_addr =
		iface ? net_if_get_link_addr(iface) : NULL;

	LOG_INF("==============================================");
	LOG_INF("Board:   %s", CONFIG_BOARD);
#if defined(CONFIG_MEMFAULT_NCS_FW_VERSION)
	LOG_INF("Version: %s", CONFIG_MEMFAULT_NCS_FW_VERSION);
#endif
	LOG_INF("Build:   %s %s", __DATE__, __TIME__);
	if (mac_addr && mac_addr->len == 6) {
		LOG_INF("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
			mac_addr->addr[0], mac_addr->addr[1], mac_addr->addr[2],
			mac_addr->addr[3], mac_addr->addr[4], mac_addr->addr[5]);
	}
	LOG_INF("----------------------------------------------");
	LOG_INF("Enabled modules:");

	/* Input */
#if CONFIG_BUTTON_MODULE
	LOG_INF("  [input]  button");
#endif
	/* CONFIG_LED_MODULE_ENABLED not yet implemented */

	/* WiFi */
#if CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED
	LOG_INF("  [wifi]   wifi_sta_prov_over_ble");
#endif
	/* CONFIG_WIFI_STA_STATIC_ENABLED   not yet implemented */
	/* CONFIG_WIFI_STA_CRED_SHELL_ENABLED not yet implemented */
	/* CONFIG_WIFI_SOFTAP_ENABLED        not yet implemented */
	/* CONFIG_WIFI_DIRECT_GO_ENABLED     not yet implemented */
	/* CONFIG_WIFI_DIRECT_CLI_ENABLED    not yet implemented */

	/* Application */
#if CONFIG_MEMFAULT_MODULE
	LOG_INF("  [app]    app_memfault");
#endif
#if CONFIG_APP_HTTPS_CLIENT_ENABLED
	LOG_INF("  [app]    app_https_client  (host: %s, interval: %ds)",
		CONFIG_APP_HTTPS_HOSTNAME, CONFIG_APP_HTTPS_REQUEST_INTERVAL_SEC);
#endif
#if CONFIG_APP_MQTT_CLIENT_ENABLED
	LOG_INF("  [app]    app_mqtt_client   (broker: %s, interval: %ds)",
		CONFIG_APP_MQTT_CLIENT_BROKER_HOSTNAME,
		CONFIG_APP_MQTT_CLIENT_PUBLISH_INTERVAL_SEC);
#endif
	LOG_INF("==============================================");

	for (;;) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
