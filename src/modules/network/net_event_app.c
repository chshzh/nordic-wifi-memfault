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
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <wifi.h>
#include "../messages.h"
#include "net_event_mgmt.h"
#if CONFIG_ZEGO_WIFI_BLE_PROV
#include <wifi_ble_prov.h>
#endif

LOG_MODULE_DECLARE(zego_net_event_mgmt, CONFIG_ZEGO_NETWORK_LOG_LEVEL);

/* -------------------------------------------------------------------------
 * Item 5: Log raw disconnect reason alongside the Zephyr mapped enum.
 *
 * The Zephyr NET_EVENT_WIFI_DISCONNECT_RESULT event carries only a mapped
 * enum (wifi_disconn_reason) which collapses many distinct 802.11 reason
 * codes into WIFI_REASON_DISCONN_UNSPECIFIED (1). The real 802.11 reason
 * code is emitted one log line earlier by wpa_supplicant:
 *   "wpa_supp: wlan0: CTRL-EVENT-DISCONNECTED bssid=... reason=<N>"
 * Common values: 6=AP table cleared, 8=AP radio restart, 34=Low ACK (PS).
 *
 * This callback fires in parallel with the zego library handler and
 * adds an explicit pointer so log readers know to look at the wpa_supp line.
 * -------------------------------------------------------------------------
 */
static struct net_mgmt_event_callback wifi_disc_reason_cb;

static void wifi_disconnect_reason_handler(struct net_mgmt_event_callback *cb,
					   uint64_t mgmt_event,
					   struct net_if *iface)
{
	ARG_UNUSED(iface);

	const struct wifi_status *status = (const struct wifi_status *)cb->info;
	int zephyr_reason = status ? (int)status->disconn_reason : -1;

	/* Zephyr reason=1 (UNSPECIFIED) masks real 802.11 codes 6/8/34.
	 * Check the preceding wpa_supp CTRL-EVENT-DISCONNECTED line for the
	 * actual 802.11 reason code.
	 */
	LOG_WRN("WiFi disconnect: zephyr_reason=%d — see wpa_supp CTRL-EVENT above for 802.11 code",
		zephyr_reason);
}

static int net_event_app_init(void)
{
	net_mgmt_init_event_callback(&wifi_disc_reason_cb,
				     wifi_disconnect_reason_handler,
				     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_disc_reason_cb);
	return 0;
}
SYS_INIT(net_event_app_init, APPLICATION, 1);

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
