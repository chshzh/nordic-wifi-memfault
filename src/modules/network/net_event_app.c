/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * net_event_app.c — memfault-specific network connectivity callbacks.
 *
 * Defines NETWORK_CHAN and APP_WIFI_STATE_CHAN, and overrides the __weak hooks
 * from zego/network/src/net_event_mgmt.c to publish on those channels.
 * When CONFIG_ZEGO_WIFI_BLE_PROV is enabled, also publishes to WIFI_CHAN so
 * the BLE advertisement reflects the current connection state.
 */

#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <wifi.h>
#include "../messages.h"
#include "net_event_mgmt.h"
#if CONFIG_ZEGO_WIFI_BLE_PROV
#include <wifi_ble_prov.h>
#endif

LOG_MODULE_DECLARE(zego_net_event_mgmt, CONFIG_ZEGO_NETWORK_LOG_LEVEL);

/* Channel definitions */
ZBUS_CHAN_DEFINE(NETWORK_CHAN, struct network_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(APP_WIFI_STATE_CHAN, struct app_wifi_state_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = APP_WIFI_STATE_CONNECTING, .mode = ZEGO_WIFI_MODE_STA));

void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *mac_addr, const char *ssid)
{
	ARG_UNUSED(ip_addr);
	ARG_UNUSED(mac_addr);
	ARG_UNUSED(ssid);

#if CONFIG_ZEGO_WIFI_BLE_PROV
	struct wifi_msg wmsg = {
		.type = WIFI_STA_CONNECTED,
		.rssi = 0,
		.error_code = 0,
	};
	int err = zbus_chan_pub(&WIFI_CHAN, &wmsg, K_MSEC(100));

	if (err) {
		LOG_WRN("Failed to publish WIFI_CHAN (CONNECTED): %d", err);
	}
#endif

	struct network_msg nmsg = {
		.type = NETWORK_READY,
		.ready = true,
	};

	int err2 = zbus_chan_pub(&NETWORK_CHAN, &nmsg, K_MSEC(100));

	if (err2) {
		LOG_WRN("Failed to publish NETWORK_CHAN (READY): %d", err2);
	}

	struct app_wifi_state_msg smsg = {
		.state = APP_WIFI_STATE_CONNECTED,
		.mode = mode,
	};

	int err3 = zbus_chan_pub(&APP_WIFI_STATE_CHAN, &smsg, K_NO_WAIT);

	if (err3) {
		LOG_WRN("Failed to publish APP_WIFI_STATE_CHAN (CONNECTED): %d", err3);
	}
}

void zego_on_net_event_wifi_disconnect(void)
{
#if CONFIG_ZEGO_WIFI_BLE_PROV
	struct wifi_msg wmsg = {
		.type = WIFI_STA_DISCONNECTED,
		.rssi = 0,
		.error_code = 0,
	};
	int err = zbus_chan_pub(&WIFI_CHAN, &wmsg, K_MSEC(100));

	if (err) {
		LOG_WRN("Failed to publish WIFI_CHAN (DISCONNECTED): %d", err);
	}
#endif

	struct network_msg nmsg = {
		.type = NETWORK_NOT_READY,
		.ready = false,
	};

	int err2 = zbus_chan_pub(&NETWORK_CHAN, &nmsg, K_MSEC(100));

	if (err2) {
		LOG_WRN("Failed to publish NETWORK_CHAN (NOT_READY): %d", err2);
	}

	struct app_wifi_state_msg smsg = {
		.state = APP_WIFI_STATE_ERROR,
		.mode = ZEGO_WIFI_MODE_STA,
	};

	int err3 = zbus_chan_pub(&APP_WIFI_STATE_CHAN, &smsg, K_NO_WAIT);

	if (err3) {
		LOG_WRN("Failed to publish APP_WIFI_STATE_CHAN (ERROR): %d", err3);
	}
}
