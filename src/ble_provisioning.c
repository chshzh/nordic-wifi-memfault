/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ble_provisioning.h"
#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
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

#include <net/wifi_prov_core/wifi_prov_core.h>
#include <bluetooth/services/wifi_provisioning.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_prov, CONFIG_MEMFAULT_SAMPLE_LOG_LEVEL);

#define WIFI_RECONNECT_DELAY_SEC 5
/* Retry every 3 minutes if reconnect fails */
#define WIFI_RECONNECT_RETRY_SEC 180

#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
#define ADV_DATA_UPDATE_INTERVAL CONFIG_WIFI_PROV_ADV_DATA_UPDATE_INTERVAL
#endif /* CONFIG_WIFI_PROV_ADV_DATA_UPDATE */

#define ADV_PARAM_UPDATE_DELAY 1

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

/* Work item for triggering WiFi connection after provisioning */
static struct k_work_delayable wifi_connect_work;
static bool wifi_connect_requested = false;

/* Track BLE connection state to avoid updating advertising while connected */
static struct bt_conn *current_conn = NULL;

static bool wifi_reconnect_pending = false;

static struct net_mgmt_event_callback wifi_mgmt_cb;

/* Flag to track if we've already requested connection after provisioning */
static bool connection_requested_after_provisioning = false;

/* Flag to indicate if credentials existed at boot time */
static bool credentials_existed_at_boot = false;

/* Track the last known provisioning state to detect new provisioning */
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
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		if (!wifi_reconnect_pending) {
			wifi_reconnect_pending = true;
			k_work_reschedule(&wifi_connect_work,
					  K_SECONDS(WIFI_RECONNECT_DELAY_SEC));
			LOG_INF("WiFi disconnected, scheduling reconnect");
		}
		break;
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
		LOG_WRN("No stored WiFi credentials, skipping reconnect "
			"attempts");
		wifi_reconnect_pending = false;
		return;
	}

	if (wifi_is_connecting) {
		LOG_DBG("WiFi connection already in progress (state %d)",
			status.state);
	} else {
		LOG_INF("WiFi credentials detected, attempting to connect");
		err = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
		if (err) {
			LOG_WRN("WiFi connection request failed: %d", err);
			if (!reconnect_cycle_active) {
				connection_requested_after_provisioning = false;
			}
		} else {
			LOG_INF("WiFi connection request sent successfully");
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

	/* Detect new provisioning: state changed from unprovisioned to
	 * provisioned */
	if (current_prov_state && !last_prov_state) {
		LOG_INF("New WiFi provisioning detected");
		/* Reset flags to allow new connection attempt */
		connection_requested_after_provisioning = false;
		credentials_existed_at_boot = false;
	}
	last_prov_state = current_prov_state;

	/* If no config, mark it as unprovisioned. */
	if (!current_prov_state) {
		prov_svc_data[ADV_DATA_FLAG_IDX] &=
			~ADV_DATA_FLAG_PROV_STATUS_BIT;
	} else {
		prov_svc_data[ADV_DATA_FLAG_IDX] |=
			ADV_DATA_FLAG_PROV_STATUS_BIT;

		/* Only trigger connection if:
		 * 1. We haven't already requested connection
		 * 2. Credentials exist
		 * 3. Credentials did NOT exist at boot (meaning they were just
		 * added via BLE)
		 * 4. WiFi is not already connected
		 */
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
					"scheduling connection "
					"attempt");
			}
		}
	}

	rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		      sizeof(struct wifi_iface_status));
	/* If WiFi is not connected or error occurs, mark it as not connected.
	 */
	if ((rc != 0) || (status.state < WIFI_STATE_ASSOCIATED)) {
		prov_svc_data[ADV_DATA_FLAG_IDX] &=
			~ADV_DATA_FLAG_CONN_STATUS_BIT;
		prov_svc_data[ADV_DATA_RSSI_IDX] = INT8_MIN;
	} else {
		/* WiFi is connected. */
		prov_svc_data[ADV_DATA_FLAG_IDX] |=
			ADV_DATA_FLAG_CONN_STATUS_BIT;
		prov_svc_data[ADV_DATA_RSSI_IDX] = status.rssi;
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("BT Connection failed (err 0x%02x)", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BT Connected: %s", addr);

	/* Store connection reference and stop advertising updates */
	current_conn = bt_conn_ref(conn);
	k_work_cancel_delayable(&update_adv_data_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BT Disconnected: %s (reason 0x%02x)", addr, reason);

	/* Release connection reference */
	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_param_work,
				    K_SECONDS(ADV_PARAM_UPDATE_DELAY));
	/* Delay ad data update until after advertising restarts to avoid EAGAIN
	 * (-11) */
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				    K_SECONDS(ADV_PARAM_UPDATE_DELAY + 1));
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_INF("BT Identity resolved %s -> %s", addr_rpa, addr_identity);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("BT Security changed: %s level %u", addr, level);
	} else {
		LOG_ERR("BT Security failed: %s level %u err %d", addr, level,
			err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.identity_resolved = identity_resolved,
	.security_changed = security_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_WRN("BT Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BT pairing completed: %s, bonded: %d", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("BT Pairing Failed (%d). Disconnecting.", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_cb_display = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void update_adv_data_task(struct k_work *item)
{
	int rc;

	/* Always update WiFi status logic (triggers connection if needed) */
	update_wifi_status_in_adv();

	/* Skip advertisement data updates if we have an active BLE connection
	 * to avoid bt_le_adv_update_data() error -11
	 */
	if (current_conn != NULL) {
		LOG_DBG("Skipping advertisement data update - BLE client "
			"connected");
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
	} else if (rc == -EAGAIN) {
		LOG_DBG("Advertisement update deferred, advertising not "
			"active");
	}
#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				    K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif /* CONFIG_WIFI_PROV_ADV_DATA_UPDATE */
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
		if (val < 10) {
			*ptr++ = (char)(val + '0');
		} else {
			*ptr++ = (char)(val - 10 + base);
		}
	}
}

static void update_dev_name(struct net_linkaddr *mac_addr)
{
	byte_to_hex(&device_name[2], mac_addr->addr[3], 'A');
	byte_to_hex(&device_name[4], mac_addr->addr[4], 'A');
	byte_to_hex(&device_name[6], mac_addr->addr[5], 'A');
}

int ble_prov_init(void)
{
	int rc;
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *mac_addr = net_if_get_link_addr(iface);
	char device_name_str[sizeof(device_name) + 1];

	/* Check if credentials already exist at boot time */
	credentials_existed_at_boot = !wifi_credentials_is_empty();
	last_prov_state = wifi_prov_state_get();

	if (credentials_existed_at_boot) {
		/* Mark connection as already requested since main.c handles it
		 */
		connection_requested_after_provisioning = true;
		LOG_INF("WiFi credentials exist at boot, skipping BLE "
			"auto-connect");
	}

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
		LOG_INF("Wi-Fi provisioning service starts successfully");
	} else {
		LOG_ERR("Error initializing Wi-Fi provisioning service");
		return rc;
	}

	/* Prepare advertisement data */
	if (mac_addr) {
		update_dev_name(mac_addr);
	}
	device_name_str[sizeof(device_name_str) - 1] = '\0';
	memcpy(device_name_str, device_name, sizeof(device_name));
	bt_set_name(device_name_str);

	rc = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] &
					     ADV_DATA_FLAG_PROV_STATUS_BIT
				     ? PROV_BT_LE_ADV_PARAM_SLOW
				     : PROV_BT_LE_ADV_PARAM_FAST,
			     ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (rc) {
		LOG_ERR("BT Advertising failed to start (err %d)", rc);
		return rc;
	}
	LOG_INF("BT Advertising successfully started");

	update_wifi_status_in_adv();

	/* Register WiFi management event handler for reconnect logic */
	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler,
				     NET_EVENT_WIFI_DISCONNECT_RESULT |
					     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	k_work_queue_init(&adv_daemon_work_q);
	k_work_queue_start(&adv_daemon_work_q, adv_daemon_stack_area,
			   K_THREAD_STACK_SIZEOF(adv_daemon_stack_area),
			   ADV_DAEMON_PRIORITY, NULL);

	k_work_init_delayable(&wifi_connect_work, wifi_connect_work_handler);
	k_work_init_delayable(&update_adv_param_work, update_adv_param_task);
	k_work_init_delayable(&update_adv_data_work, update_adv_data_task);
#ifdef CONFIG_WIFI_PROV_ADV_DATA_UPDATE
	k_work_schedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				  K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif /* CONFIG_WIFI_PROV_ADV_DATA_UPDATE */

	return 0;
}

void ble_prov_update_wifi_status(bool connected)
{
	/* Reset connection request flag when WiFi connects */
	if (connected) {
		wifi_connect_requested = false;
		wifi_reconnect_pending = false;
	}

	/* Update advertisement data with current WiFi status */
	k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
				    K_NO_WAIT);
}
