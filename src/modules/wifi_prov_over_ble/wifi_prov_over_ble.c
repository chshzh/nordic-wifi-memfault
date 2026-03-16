/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "wifi_prov_over_ble.h"
#include "../messages.h"

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
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <net/wifi_prov_core/wifi_prov_core.h>
#include <bluetooth/services/wifi_provisioning.h>

LOG_MODULE_REGISTER(wifi_prov_over_ble, CONFIG_WIFI_PROV_OVER_BLE_LOG_LEVEL);

#define WIFI_RECONNECT_DELAY_SEC 5
#define WIFI_RECONNECT_RETRY_SEC 180

#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
#define ADV_DATA_UPDATE_INTERVAL CONFIG_WIFI_PROV_ADV_DATA_UPDATE_INTERVAL
#endif

#define ADV_PARAM_UPDATE_DELAY        1
#define ADV_DATA_VERSION_IDX          (BT_UUID_SIZE_128 + 0)
#define ADV_DATA_FLAG_IDX             (BT_UUID_SIZE_128 + 1)
#define ADV_DATA_FLAG_PROV_STATUS_BIT BIT(0)
#define ADV_DATA_FLAG_CONN_STATUS_BIT BIT(1)
#define ADV_DATA_RSSI_IDX             (BT_UUID_SIZE_128 + 3)

#define PROV_BT_LE_ADV_PARAM_FAST                                              \
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2,         \
			BT_GAP_ADV_FAST_INT_MAX_2, NULL)
#define PROV_BT_LE_ADV_PARAM_SLOW                                              \
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_SLOW_INT_MIN,           \
			BT_GAP_ADV_SLOW_INT_MAX, NULL)

#define ADV_DAEMON_STACK_SIZE 4096
#define ADV_DAEMON_PRIORITY   5

static struct k_work_delayable wifi_connect_work;
static bool wifi_connect_requested = false;
static struct bt_conn *current_conn = NULL;
static bool wifi_reconnect_pending = false;
static struct net_mgmt_event_callback wifi_mgmt_cb;
static bool connection_requested_after_provisioning = false;
static bool credentials_existed_at_boot = false;
static bool last_prov_state = false;

K_THREAD_STACK_DEFINE(adv_daemon_stack_area, ADV_DAEMON_STACK_SIZE);
static struct k_work_q adv_daemon_work_q;

static uint8_t device_name[] = {'P', 'V', '0', '0', '0', '0', '0', '0'};
static uint8_t prov_svc_data[] = {BT_UUID_PROV_VAL, 0x00, 0x00, 0x00, 0x00};

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

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);
	if (!wifi_prov_state_get()) {
		return;
	}
	switch (mgmt_event) {
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		const struct wifi_status *status =
			(const struct wifi_status *)cb->info;
		/*
		 * Skip auto-reconnect for provisioner-initiated disconnects
		 * (status == 0 means intentional/locally-generated, which is
		 * what the provisioner does before a WiFi scan).  The
		 * provisioner library owns reconnect in that case; scheduling
		 * our own reconnect work races against it and can crash the
		 * WPA supplicant.
		 */
		if (status && status->status == 0) {
			LOG_INF("WiFi disconnected (intentional), deferring "
				"reconnect to provisioner");
			break;
		}
		if (!wifi_reconnect_pending) {
			wifi_reconnect_pending = true;
			k_work_reschedule(&wifi_connect_work,
					  K_SECONDS(WIFI_RECONNECT_DELAY_SEC));
			LOG_INF("WiFi disconnected, scheduling reconnect");
		}
		break;
	}
	case NET_EVENT_WIFI_CONNECT_RESULT:
		wifi_reconnect_pending = false;
		k_work_cancel_delayable(&wifi_connect_work);
		break;
	default:
		break;
	}
}

static void wifi_connect_work_handler(struct k_work *work)
{
	int err;
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = {0};
	int status_rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
				 sizeof(status));
	bool wifi_is_connected =
		(status_rc == 0 && status.state >= WIFI_STATE_ASSOCIATED);
	bool wifi_is_connecting =
		(status_rc == 0 && status.state > WIFI_STATE_DISCONNECTED &&
		 status.state < WIFI_STATE_ASSOCIATED);
	bool reconnect_cycle_active = wifi_reconnect_pending;

	if (wifi_is_connected) {
		wifi_reconnect_pending = false;
		return;
	}
	if (wifi_credentials_is_empty()) {
		LOG_WRN("No stored WiFi credentials, skipping reconnect");
		wifi_reconnect_pending = false;
		return;
	}
	/*
	 * If the provisioner just set credentials and is driving its own
	 * internal reconnect (wifi_prov_state_get() == true), don't race it
	 * with a second NET_REQUEST_WIFI_CONNECT_STORED call.  Reschedule as
	 * a fallback; the provisioner's NET_EVENT_WIFI_CONNECT_RESULT handler
	 * will cancel this work if reconnect succeeds first.
	 */
	if (wifi_prov_state_get()) {
		LOG_INF("Provisioner active, deferring connect attempt");
		k_work_reschedule(&wifi_connect_work,
				  K_SECONDS(WIFI_RECONNECT_DELAY_SEC));
		return;
	}
	if (wifi_is_connecting) {
		LOG_DBG("WiFi connection in progress (state %d)", status.state);
	} else {
		LOG_INF("WiFi credentials detected, attempting to connect");
		err = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
		if (err) {
			LOG_WRN("WiFi connection request failed: %d", err);
			if (!reconnect_cycle_active) {
				connection_requested_after_provisioning = false;
			}
		} else {
			LOG_INF("WiFi connection request sent");
		}
	}
	if (reconnect_cycle_active) {
		k_work_reschedule(&wifi_connect_work,
				  K_SECONDS(WIFI_RECONNECT_RETRY_SEC));
		LOG_INF("WiFi still disconnected, retrying in %d seconds",
			WIFI_RECONNECT_RETRY_SEC);
	}
}

static void update_wifi_status_in_adv(void)
{
	int rc;
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = {0};
	bool current_prov_state;

	prov_svc_data[ADV_DATA_VERSION_IDX] = PROV_SVC_VER;
	current_prov_state = wifi_prov_state_get();

	if (current_prov_state && !last_prov_state) {
		LOG_INF("New WiFi provisioning detected");
		connection_requested_after_provisioning = false;
		credentials_existed_at_boot = false;
	}
	last_prov_state = current_prov_state;

	if (!current_prov_state) {
		prov_svc_data[ADV_DATA_FLAG_IDX] &=
			~ADV_DATA_FLAG_PROV_STATUS_BIT;
	} else {
		prov_svc_data[ADV_DATA_FLAG_IDX] |=
			ADV_DATA_FLAG_PROV_STATUS_BIT;
		if (!connection_requested_after_provisioning &&
		    !wifi_credentials_is_empty() &&
		    !credentials_existed_at_boot) {
			rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface,
				      &status, sizeof(status));
			bool wifi_is_connected =
				(rc == 0 &&
				 status.state >= WIFI_STATE_ASSOCIATED);
			if (!wifi_is_connected) {
				connection_requested_after_provisioning = true;
				k_work_reschedule(&wifi_connect_work,
						  K_SECONDS(2));
				LOG_INF("WiFi credentials provisioned, "
					"scheduling connection");
			}
		}
	}

	rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		      sizeof(struct wifi_iface_status));
	if ((rc != 0) || (status.state < WIFI_STATE_ASSOCIATED)) {
		prov_svc_data[ADV_DATA_FLAG_IDX] &=
			~ADV_DATA_FLAG_CONN_STATUS_BIT;
		prov_svc_data[ADV_DATA_RSSI_IDX] = INT8_MIN;
	} else {
		prov_svc_data[ADV_DATA_FLAG_IDX] |=
			ADV_DATA_FLAG_CONN_STATUS_BIT;
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
	k_work_cancel_delayable(&update_adv_data_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("BT Disconnected (reason 0x%02x)", reason);
	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_param_work,
				    K_SECONDS(ADV_PARAM_UPDATE_DELAY));
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				    K_SECONDS(ADV_PARAM_UPDATE_DELAY + 1));
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(rpa);
	ARG_UNUSED(identity);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
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
		k_work_reschedule_for_queue(
			&adv_daemon_work_q, &update_adv_data_work,
			K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif
		return;
	}
	rc = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (rc != 0 && rc != -EAGAIN) {
		LOG_ERR("Cannot update advertisement data, err = %d", rc);
	}
#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				    K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
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
	rc = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] &
					     ADV_DATA_FLAG_PROV_STATUS_BIT
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
	struct net_linkaddr *mac_addr =
		iface ? net_if_get_link_addr(iface) : NULL;
	char device_name_str[sizeof(device_name) + 1];

	credentials_existed_at_boot = !wifi_credentials_is_empty();
	last_prov_state = wifi_prov_state_get();
	if (credentials_existed_at_boot) {
		connection_requested_after_provisioning = true;
		LOG_INF("WiFi credentials exist at boot, skipping BLE "
			"auto-connect");
	}

	k_work_queue_init(&adv_daemon_work_q);
	k_work_queue_start(&adv_daemon_work_q, adv_daemon_stack_area,
			   K_THREAD_STACK_SIZEOF(adv_daemon_stack_area),
			   ADV_DAEMON_PRIORITY, NULL);
	k_work_init_delayable(&wifi_connect_work, wifi_connect_work_handler);
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

	rc = wifi_prov_init();
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

	rc = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] &
					     ADV_DATA_FLAG_PROV_STATUS_BIT
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

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler,
				     NET_EVENT_WIFI_DISCONNECT_RESULT |
					     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);
#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
	k_work_schedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				  K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif
	return 0;
}

void wifi_prov_over_ble_update_wifi_status(bool connected)
{
	if (connected) {
		wifi_connect_requested = false;
		wifi_reconnect_pending = false;
	}
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				    K_NO_WAIT);
}

/* Zbus: update BLE advertisement when WiFi connect/disconnect (from wifi
 * module) */
extern const struct zbus_channel WIFI_CHAN;

static void wifi_prov_over_ble_listener(const struct zbus_channel *chan)
{
	const struct wifi_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == WIFI_STA_CONNECTED) {
		LOG_INF("WiFi connected - BLE advertisement updated");
		wifi_prov_over_ble_update_wifi_status(true);
	} else if (msg->type == WIFI_STA_DISCONNECTED) {
		LOG_INF("WiFi disconnected - BLE advertisement updated");
		wifi_prov_over_ble_update_wifi_status(false);
	}
}

ZBUS_LISTENER_DEFINE(wifi_prov_over_ble_listener_def,
		     wifi_prov_over_ble_listener);
ZBUS_CHAN_ADD_OBS(WIFI_CHAN, wifi_prov_over_ble_listener_def, 0);

/* Initialize BLE provisioning after network event module init (default 90). */
SYS_INIT(wifi_prov_over_ble_init, APPLICATION, 95);
