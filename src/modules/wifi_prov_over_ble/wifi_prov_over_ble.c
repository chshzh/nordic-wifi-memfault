/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "wifi_prov_over_ble.h"
#include "../messages.h"
#include "../network/net_event_mgmt.h"
#include "../network/wifi_utils.h"

#include <stdbool.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <bluetooth/services/wifi_provisioning.h>
#include <net/wifi_mgmt_ext.h>

LOG_MODULE_REGISTER(wifi_prov_over_ble, CONFIG_WIFI_PROV_OVER_BLE_LOG_LEVEL);

#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
#define ADV_DATA_UPDATE_INTERVAL CONFIG_WIFI_PROV_ADV_DATA_UPDATE_INTERVAL
#endif

#define ADV_PARAM_UPDATE_DELAY        1
#define ADV_DATA_VERSION_IDX          (BT_UUID_SIZE_128 + 0)
#define ADV_DATA_FLAG_IDX             (BT_UUID_SIZE_128 + 1)
#define ADV_DATA_FLAG_PROV_STATUS_BIT BIT(0)
#define ADV_DATA_FLAG_CONN_STATUS_BIT BIT(1)
#define ADV_DATA_RSSI_IDX             (BT_UUID_SIZE_128 + 3)

#define PROV_BT_LE_ADV_PARAM_FAST                                                                  \
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, BT_GAP_ADV_FAST_INT_MIN_2,                      \
			BT_GAP_ADV_FAST_INT_MAX_2, NULL)
#define PROV_BT_LE_ADV_PARAM_SLOW                                                                  \
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, BT_GAP_ADV_SLOW_INT_MIN,                        \
			BT_GAP_ADV_SLOW_INT_MAX, NULL)

static bool wifi_connect_requested = false;
static struct bt_conn *current_conn = NULL;
static bool connection_requested_after_provisioning = false;
static bool credentials_existed_at_boot = false;
static bool last_prov_state = false;

static uint8_t device_name[] = {'P', 'V', '0', '0', '0', '0', '0', '0'};
static uint8_t prov_svc_data[] = {BT_UUID_PROV_VAL, 0x00, 0x00, 0x00, 0x00};

extern const struct zbus_channel BLE_CHAN;

static void publish_ble_event(enum ble_msg_type type)
{
	struct ble_msg msg = {.type = type};
	int err = zbus_chan_pub(&BLE_CHAN, &msg, K_MSEC(100));

	if (err) {
		LOG_WRN("Failed to publish BLE_CHAN event (%d)", err);
	}
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_PROV_VAL),
	BT_DATA(BT_DATA_NAME_COMPLETE, device_name, sizeof(device_name)),
};
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_SVC_DATA128, prov_svc_data, sizeof(prov_svc_data)),
};

static struct k_work_delayable update_adv_param_work;
static struct k_work_delayable update_adv_data_work;

static void update_wifi_status_in_adv(void)
{
	int rc;
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = {0};
	bool current_prov_state;

	prov_svc_data[ADV_DATA_VERSION_IDX] = PROV_SVC_VER;
	current_prov_state = bt_wifi_prov_state_get();

	if (current_prov_state && !last_prov_state) {
		LOG_INF("New WiFi provisioning detected");
		connection_requested_after_provisioning = false;
		credentials_existed_at_boot = false;
	}
	last_prov_state = current_prov_state;

	if (!current_prov_state) {
		prov_svc_data[ADV_DATA_FLAG_IDX] &= ~ADV_DATA_FLAG_PROV_STATUS_BIT;
	} else {
		prov_svc_data[ADV_DATA_FLAG_IDX] |= ADV_DATA_FLAG_PROV_STATUS_BIT;
		if (iface && !connection_requested_after_provisioning &&
		    wifi_utils_has_stored_credentials() && !credentials_existed_at_boot) {
			rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
				      sizeof(status));
			bool wifi_is_connected = (rc == 0 && status.state >= WIFI_STATE_ASSOCIATED);
			if (!wifi_is_connected) {
				connection_requested_after_provisioning = true;
				net_event_mgmt_request_connect();
				LOG_INF("WiFi credentials provisioned, "
					"scheduling connection");
			}
		}
	}

	/* iface may still be NULL very early at boot (this function is called
	 * once synchronously from wifi_prov_over_ble_init(), a SYS_INIT hook
	 * that can run before the nrf700x net_if is ready) - net_mgmt()
	 * dereferences iface internally, so treat "no iface yet" the same as
	 * "not connected" rather than calling net_mgmt() with iface == NULL.
	 */
	rc = iface ? net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
			      sizeof(struct wifi_iface_status))
		   : -ENODEV;
	if ((rc != 0) || (status.state < WIFI_STATE_ASSOCIATED)) {
		prov_svc_data[ADV_DATA_FLAG_IDX] &= ~ADV_DATA_FLAG_CONN_STATUS_BIT;
		prov_svc_data[ADV_DATA_RSSI_IDX] = INT8_MIN;
	} else {
		prov_svc_data[ADV_DATA_FLAG_IDX] |= ADV_DATA_FLAG_CONN_STATUS_BIT;
		prov_svc_data[ADV_DATA_RSSI_IDX] = status.rssi;
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("BT Connection failed (err 0x%02x)", err);
		return;
	}
	LOG_INF("BT Connected");
	current_conn = bt_conn_ref(conn);
	publish_ble_event(BLE_CLIENT_CONNECTED);
	k_work_cancel_delayable(&update_adv_data_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("BT Disconnected (reason 0x%02x)", reason);
	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	publish_ble_event(BLE_CLIENT_DISCONNECTED);
	k_work_reschedule(&update_adv_param_work, K_SECONDS(ADV_PARAM_UPDATE_DELAY));
	k_work_reschedule(&update_adv_data_work, K_SECONDS(ADV_PARAM_UPDATE_DELAY + 1));
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(rpa);
	ARG_UNUSED(identity);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(level);
	ARG_UNUSED(err);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.identity_resolved = identity_resolved,
	.security_changed = security_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	LOG_WRN("BT Pairing cancelled");
}

static struct bt_conn_auth_cb auth_cb_display = {.cancel = auth_cancel};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(bonded);
	LOG_INF("BT pairing completed");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("BT Pairing Failed (%d)", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_cb_display = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void update_adv_data_task(struct k_work *item)
{
	int rc;

	update_wifi_status_in_adv();
	if (current_conn != NULL) {
#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
		k_work_reschedule(&update_adv_data_work, K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif
		return;
	}
	rc = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (rc != 0 && rc != -EAGAIN) {
		LOG_ERR("Cannot update advertisement data, err = %d", rc);
	}
#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
	k_work_reschedule(&update_adv_data_work, K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif
}

static void update_adv_param_task(struct k_work *item)
{
	int rc;

	rc = bt_le_adv_stop();
	if (rc != 0) {
		LOG_ERR("Cannot stop advertisement: err = %d", rc);
		return;
	}
	rc = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] & ADV_DATA_FLAG_PROV_STATUS_BIT
				     ? PROV_BT_LE_ADV_PARAM_SLOW
				     : PROV_BT_LE_ADV_PARAM_FAST,
			     ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (rc != 0) {
		LOG_ERR("Cannot start advertisement: err = %d", rc);
	}
}

static void byte_to_hex(char *ptr, uint8_t byte, char base)
{
	int i, val;
	for (i = 0, val = (byte & 0xf0) >> 4; i < 2; i++, val = byte & 0x0f) {
		*ptr++ = (char)(val < 10 ? val + '0' : val - 10 + base);
	}
}

static void update_dev_name(struct net_linkaddr *mac_addr)
{
	byte_to_hex(&device_name[2], mac_addr->addr[3], 'A');
	byte_to_hex(&device_name[4], mac_addr->addr[4], 'A');
	byte_to_hex(&device_name[6], mac_addr->addr[5], 'A');
}

int wifi_prov_over_ble_init(void)
{
	int rc;
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *mac_addr = iface ? net_if_get_link_addr(iface) : NULL;
	char device_name_str[sizeof(device_name) + 1];

	/* Boot-time auto-connect using stored credentials is handled by
	 * network module (net_event_mgmt.c), which owns the only
	 * NET_REQUEST_WIFI_CONNECT_STORED call path in the app. This flag is
	 * only used here to gate re-triggering a connect right after a fresh
	 * BLE provisioning event (see update_wifi_status_in_adv()).
	 */
	credentials_existed_at_boot = wifi_utils_has_stored_credentials();
	last_prov_state = bt_wifi_prov_state_get();
	if (credentials_existed_at_boot) {
		connection_requested_after_provisioning = true;
	}

	k_work_init_delayable(&update_adv_param_work, update_adv_param_task);
	k_work_init_delayable(&update_adv_data_work, update_adv_data_task);

	bt_conn_auth_cb_register(&auth_cb_display);
	bt_conn_auth_info_cb_register(&auth_info_cb_display);

	rc = bt_enable(NULL);
	if (rc) {
		LOG_ERR("Bluetooth init failed (err %d)", rc);
		return rc;
	}
	LOG_INF("Bluetooth initialized");

	rc = bt_wifi_prov_init();
	if (rc == 0) {
		LOG_INF("Wi-Fi provisioning service started");
	} else {
		LOG_ERR("Error initializing Wi-Fi provisioning service");
		return rc;
	}

	if (mac_addr && mac_addr->len >= 6U) {
		update_dev_name(mac_addr);
	}
	device_name_str[sizeof(device_name_str) - 1] = '\0';
	memcpy(device_name_str, device_name, sizeof(device_name));
	bt_set_name(device_name_str);

	update_wifi_status_in_adv();

	rc = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] & ADV_DATA_FLAG_PROV_STATUS_BIT
				     ? PROV_BT_LE_ADV_PARAM_SLOW
				     : PROV_BT_LE_ADV_PARAM_FAST,
			     ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (rc) {
		LOG_ERR("BT Advertising failed to start (err %d)", rc);
		return rc;
	}
	LOG_INF("BT Advertising started");
	LOG_INF("********************************************");
	LOG_INF("* BLE PROVISIONING READY");
	LOG_INF("* Device Name: %s", device_name_str);
	LOG_INF("* Open 'nRF Wi-Fi Provisioner' app to");
	LOG_INF("* connect and provision WiFi credentials");
	LOG_INF("********************************************");

#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
	k_work_schedule(&update_adv_data_work, K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif
	return 0;
}

void wifi_prov_over_ble_update_wifi_status(bool connected)
{
	if (connected) {
		wifi_connect_requested = false;
	}
	k_work_reschedule(&update_adv_data_work, K_NO_WAIT);
}

/* Zbus: update BLE advertisement when the network becomes ready/not-ready
 * (from network module) */
extern const struct zbus_channel NETWORK_CHAN;

static void wifi_prov_over_ble_listener(const struct zbus_channel *chan)
{
	const struct network_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == NETWORK_READY) {
		LOG_INF("Network ready - BLE advertisement updated");
		wifi_prov_over_ble_update_wifi_status(true);
	} else if (msg->type == NETWORK_NOT_READY) {
		LOG_INF("Network not ready - BLE advertisement updated");
		wifi_prov_over_ble_update_wifi_status(false);
	}
}

ZBUS_LISTENER_DEFINE(wifi_prov_over_ble_listener_def, wifi_prov_over_ble_listener);
ZBUS_CHAN_ADD_OBS(NETWORK_CHAN, wifi_prov_over_ble_listener_def, 0);

/* Initialize BLE provisioning after network event module init (default 90). */
SYS_INIT(wifi_prov_over_ble_init, APPLICATION, 95);
