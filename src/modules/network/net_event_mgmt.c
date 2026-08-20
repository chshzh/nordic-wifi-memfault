/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <supp_events.h>
#include <zephyr/net/socket.h>
#include <net/wifi_mgmt_ext.h>
#include <inttypes.h>
#include <errno.h>
#include <zephyr/zbus/zbus.h>

#include "../messages.h"

#include "net_event_mgmt.h"
#include "wifi_utils.h"

LOG_MODULE_REGISTER(net_event_mgmt, CONFIG_WIFI_MODULE_LOG_LEVEL);

ZBUS_CHAN_DEFINE(WIFI_CHAN, struct wifi_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(NETWORK_CHAN, struct network_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
/* Defined here (rather than in wifi_prov_over_ble.c) because this module's STA
 * reconnect logic is the consumer and must keep compiling/working regardless
 * of whether CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED is set; wifi_prov_over_ble.c
 * publishes to it only when compiled in.
 */
ZBUS_CHAN_DEFINE(BLE_CHAN, struct ble_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* Tracks whether a BLE client is currently connected, so STA reconnect can
 * avoid racing a BLE provisioning session's own connect attempts. Defaults to
 * "not connected" when wifi_prov_over_ble is not compiled in.
 */
static bool ble_client_connected;

static void ble_chan_listener_cb(const struct zbus_channel *chan)
{
	const struct ble_msg *msg = zbus_chan_const_msg(chan);

	ble_client_connected = (msg->type == BLE_CLIENT_CONNECTED);
}

ZBUS_LISTENER_DEFINE(net_event_mgmt_ble_listener, ble_chan_listener_cb);
ZBUS_CHAN_ADD_OBS(BLE_CHAN, net_event_mgmt_ble_listener, 0);

static void publish_wifi_event(enum wifi_msg_type type, int error_code)
{
	struct wifi_msg msg = {
		.type = type,
		.rssi = 0,
		.error_code = error_code,
	};

	int err = zbus_chan_pub(&WIFI_CHAN, &msg, K_MSEC(100));
	if (err) {
		LOG_WRN("Failed to publish WIFI_CHAN event (%d)", err);
	}
}

static void publish_network_ready(bool ready)
{
	struct network_msg msg = {
		.type = ready ? NETWORK_READY : NETWORK_NOT_READY,
		.ready = ready,
	};

	int err = zbus_chan_pub(&NETWORK_CHAN, &msg, K_MSEC(100));
	if (err) {
		LOG_WRN("Failed to publish NETWORK_CHAN event (%d)", err);
	}
}

/* Event masks for different network layers */
#define L2_IF_EVENT_MASK ((uint32_t)(NET_EVENT_IF_DOWN | NET_EVENT_IF_UP))
#define L2_WIFI_CONN_EVENT_MASK                                                                    \
	((uint32_t)(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT))
/* NCS v2.6.4 names these NET_EVENT_WPA_SUPP_READY/NOT_READY (renamed to
 * NET_EVENT_SUPPLICANT_READY/NOT_READY in a later NCS release). */
#define L3_WPA_SUPP_EVENT_MASK ((uint32_t)(NET_EVENT_WPA_SUPP_READY | NET_EVENT_WPA_SUPP_NOT_READY))
/* Comprehensive IPv4/DHCP event mask for complete lifecycle tracking */
#define L3_IPV4_EVENT_MASK                                                                         \
	((uint32_t)(NET_EVENT_IPV4_DHCP_START | NET_EVENT_IPV4_DHCP_BOUND |                        \
		    NET_EVENT_IPV4_DHCP_STOP | NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL))

/* Define network event semaphores */
K_SEM_DEFINE(iface_up_sem, 0, 1);
K_SEM_DEFINE(wpa_supplicant_ready_sem, 0, 1);
K_SEM_DEFINE(ipv4_dhcp_bond_sem, 0, 1);

/* Track network connectivity state (WiFi connected + IP assigned) */
static bool network_connected;

#if CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC > 0
/* L3 connectivity watchdog: NET_EVENT_WIFI_CONNECT_RESULT success only means
 * the link is up at L2 (802.11 association) -- it does NOT mean an IP was
 * obtained. Without this, a STA that associates but never gets a DHCP lease
 * (or whose lease is later lost while the link stays associated) sits
 * "associated, no IP" forever, since nothing else re-triggers a reconnect.
 * Armed on L2 connect success and on lease loss (ADDR_DEL); cancelled on
 * DHCP_BOUND and on DISCONNECT_RESULT. On expiry it issues
 * NET_REQUEST_WIFI_DISCONNECT, which produces a DISCONNECT_RESULT that
 * re-arms this module's own STA reconnect logic below.
 */
static void l3_dhcp_watchdog_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(l3_dhcp_watchdog_work, l3_dhcp_watchdog_handler);

static void l3_dhcp_watchdog_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_default();

	if (network_connected || iface == NULL) {
		return;
	}

	LOG_WRN("L3 watchdog: associated but no IP after %d s - forcing reconnect",
		CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC);

	int rc = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);

	if (rc) {
		LOG_ERR("L3 watchdog: NET_REQUEST_WIFI_DISCONNECT failed (%d)", rc);
	}
}

static void l3_dhcp_watchdog_arm(void)
{
	k_work_reschedule(&l3_dhcp_watchdog_work, K_SECONDS(CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC));
}

static void l3_dhcp_watchdog_cancel(void)
{
	k_work_cancel_delayable(&l3_dhcp_watchdog_work);
}
#else
static inline void l3_dhcp_watchdog_arm(void)
{
}
static inline void l3_dhcp_watchdog_cancel(void)
{
}
#endif /* CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC > 0 */

/* STA auto-reconnect. This module owns the only NET_REQUEST_WIFI_CONNECT_STORED
 * call path in the app, so boot-time auto-connect and reconnect-after-disconnect
 * work regardless of which credential-entry mechanism (BLE provisioning,
 * wifi_cred_shell, static config) is enabled.
 * First attempt after a disconnect fires quickly (WIFI_RECONNECT_DELAY_SEC);
 * every subsequent retry uses a flat WIFI_RECONNECT_RETRY_SEC interval.
 */
#define WIFI_RECONNECT_DELAY_SEC 5
#define WIFI_RECONNECT_RETRY_SEC 180U

/* Sized to accommodate NET_REQUEST_WIFI_CONNECT_STORED's full connect chain
 * (net_mgmt -> wifi mgmt handler -> nrf700x driver -> hostap/wpa_supplicant
 * event loop, which itself calls zsock_select()/poll() internally) - this
 * needs comparable headroom to CONFIG_WPA_SUPP_THREAD_STACK_SIZE elsewhere in
 * this project. A 4096 B queue previously overflowed with "Stack overflow
 * (context area not valid)" inside zsock_poll_internal() when this work ran
 * on it (originally discovered while this logic lived in wifi_prov_over_ble.c).
 */
#define NET_CONNECT_STACK_SIZE     8192
#define NET_CONNECT_WORKQ_PRIORITY 5

K_THREAD_STACK_DEFINE(net_connect_stack_area, NET_CONNECT_STACK_SIZE);
static struct k_work_q net_connect_work_q;
static struct k_work_delayable wifi_connect_work;
static bool wifi_reconnect_pending;

static void wifi_connect_work_handler(struct k_work *work)
{
	int err;
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = {0};
	int status_rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
	bool wifi_is_connected = (status_rc == 0 && status.state >= WIFI_STATE_ASSOCIATED);
	bool wifi_is_connecting = (status_rc == 0 && status.state > WIFI_STATE_DISCONNECTED &&
				   status.state < WIFI_STATE_ASSOCIATED);
	bool reconnect_cycle_active = wifi_reconnect_pending;

	if (wifi_is_connected) {
		wifi_reconnect_pending = false;
		return;
	}
	if (!wifi_utils_has_stored_credentials()) {
		LOG_WRN("No stored WiFi credentials, skipping reconnect");
		wifi_reconnect_pending = false;
		return;
	}
	/*
	 * If a BLE client is actively connected, it may be in the middle of
	 * provisioning and driving its own internal reconnect - don't race it
	 * with a second NET_REQUEST_WIFI_CONNECT_STORED call. Reschedule as a
	 * fallback; this module's own NET_EVENT_WIFI_CONNECT_RESULT handler
	 * will cancel this work if reconnect succeeds first.
	 */
	if (ble_client_connected) {
		LOG_INF("BLE client connected, deferring connect attempt");
		k_work_reschedule_for_queue(&net_connect_work_q, &wifi_connect_work,
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
		} else {
			LOG_INF("WiFi connection request sent");
		}
	}
	if (reconnect_cycle_active) {
		k_work_reschedule_for_queue(&net_connect_work_q, &wifi_connect_work,
					    K_SECONDS(WIFI_RECONNECT_RETRY_SEC));
		LOG_INF("WiFi still disconnected, retrying in %u seconds",
			WIFI_RECONNECT_RETRY_SEC);
	}
}

void net_event_mgmt_request_connect(void)
{
	k_work_reschedule_for_queue(&net_connect_work_q, &wifi_connect_work, K_SECONDS(2));
}

/* Function declarations */

/* Declare the callback structures for Wi-Fi and network events */
static struct net_mgmt_event_callback iface_event_cb;
static struct net_mgmt_event_callback wpa_event_cb;
static struct net_mgmt_event_callback wifi_event_cb;
static struct net_mgmt_event_callback ipv4_event_cb;

static void l2_iface_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
				   struct net_if *iface)
{
	char ifname[IFNAMSIZ + 1] = {0};
	int ret;
	switch (mgmt_event) {
	case NET_EVENT_IF_UP:
		ret = net_if_get_name(iface, ifname, sizeof(ifname) - 1);
		if (ret < 0) {
			LOG_ERR("[IF] Cannot get interface %d (%p) name",
				net_if_get_by_iface(iface), iface);
		}
		LOG_INF("[IF] Network interface %s is up", ifname);
		k_sem_give(&iface_up_sem);
		break;
	case NET_EVENT_IF_DOWN:
		ret = net_if_get_name(iface, ifname, sizeof(ifname) - 1);
		if (ret < 0) {
			LOG_ERR("[IF] Cannot get interface %d (%p) name",
				net_if_get_by_iface(iface), iface);
		}
		LOG_INF("[IF] Network interface %s is down", ifname);
		break;
	default:
		LOG_DBG("[IF] Unhandled network interface event: 0x%08" PRIx32, mgmt_event);
		break;
	}
}

/* Enhanced WiFi management event handler for L2 events */
static void l2_wifi_conn_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
				       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status->status == 0) {
			/* Connection successful */
			LOG_INF("[WiFi] WiFi is connected!");
			/* Print detailed WiFi status when connected */
			wifi_print_status();
			publish_wifi_event(WIFI_STA_ASSOCIATED, 0);
			/* Associated at L2 - arm the watchdog; it is cancelled when
			 * DHCP binds.
			 */
			l3_dhcp_watchdog_arm();
			/* Genuine success - clear reconnect state and cancel any
			 * pending retry. A failed/timed-out attempt (status != 0,
			 * handled below) must NOT do this, otherwise a single
			 * failed attempt mid-retry-cycle would silently end all
			 * further reconnect attempts.
			 */
			wifi_reconnect_pending = false;
			k_work_cancel_delayable(&wifi_connect_work);
		} else {
			/* Decode common error codes */
			switch (status->status) {
			case 1:
				/* Transient: supplicant will auto-retry */
				LOG_WRN("[WiFi] Reason: Generic failure "
					"(retrying)");
				break;
			case 2:
				/* Transient: EAPOL handshake timeout, retrying */
				LOG_WRN("[WiFi] Reason: Authentication timeout "
					"(retrying)");
				break;
			case 3:
				LOG_ERR("[WiFi] Reason: Authentication failed");
				break;
			case 15:
				LOG_ERR("[WiFi] Reason: AP not found");
				break;
			case 16:
				/* Transient: association timeout, retrying */
				LOG_WRN("[WiFi] Reason: Association timeout "
					"(retrying)");
				break;
			case -ETIMEDOUT:
				LOG_ERR("[WiFi] Reason: Connection timed out "
					"(-ETIMEDOUT), please check "
					"your WiFi credentials or if the AP is "
					"available");
				break;
			default:
				LOG_ERR("[WiFi] Reason: Unknown error code %d", status->status);
				break;
			}

			publish_wifi_event(WIFI_ERROR, status->status);
		}
	} break;

	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status) {
			LOG_WRN("=== WiFi DISCONNECTED (reason: %d) ===", status->status);

			/* Common disconnect reasons (from WiFi spec) */
			switch (status->status) {
			case 0:
				LOG_INF("[WiFi] Reason: Success (intentional "
					"disconnect)");
				break;
			case 1:
				LOG_WRN("[WiFi] Reason: Unspecified");
				break;
			case 2:
				LOG_WRN("[WiFi] Reason: Previous auth no "
					"longer valid");
				break;
			case 3:
				LOG_WRN("[WiFi] Reason: Deauth - leaving "
					"network");
				break;
			case 4:
				LOG_WRN("[WiFi] Reason: Disassoc - inactivity "
					"timeout");
				break;
			case 6:
				LOG_WRN("[WiFi] Reason: Class 2 frame from "
					"non-auth STA");
				break;
			case 7:
				LOG_WRN("[WiFi] Reason: Class 3 frame from "
					"non-assoc STA");
				break;
			case 8:
				LOG_WRN("[WiFi] Reason: Disassoc - STA "
					"leaving");
				break;
			case 15:
				LOG_WRN("[WiFi] Reason: 4-way handshake "
					"timeout");
				break;
			case 16:
				LOG_WRN("[WiFi] Reason: Group key handshake "
					"timeout");
				break;
			case 23:
				LOG_ERR("[WiFi] Reason: IEEE 802.1X auth "
					"failed");
				break;
			default:
				LOG_WRN("[WiFi] Reason: Unknown (%d)", status->status);
				break;
			}
		} else {
			LOG_WRN("[WiFi] WiFi disconnected: status=NULL");
		}

		/* Link is down - stand the watchdog down so it does not
		 * double-trigger against the reconnect path taking over.
		 */
		l3_dhcp_watchdog_cancel();
		network_connected = false;
		publish_wifi_event(WIFI_STA_DISCONNECTED, status ? status->status : -1);
		publish_network_ready(false);

		/*
		 * status == 0 (intentional/locally-generated) is ambiguous: it's
		 * what a BLE provisioning session does before a WiFi scan, but
		 * it's also what this module's own L3 DHCP watchdog gets back
		 * from its own NET_REQUEST_WIFI_DISCONNECT above. Only defer to
		 * the BLE provisioner when a client is actually connected and
		 * could be driving that disconnect itself - otherwise nothing
		 * else ever reconnects and the device is stuck offline forever.
		 */
		if (status && status->status == 0 && ble_client_connected) {
			LOG_INF("WiFi disconnected (intentional), deferring "
				"reconnect to BLE provisioner");
		} else if (!wifi_reconnect_pending) {
			wifi_reconnect_pending = true;
			k_work_reschedule_for_queue(&net_connect_work_q, &wifi_connect_work,
						    K_SECONDS(WIFI_RECONNECT_DELAY_SEC));
			LOG_INF("WiFi disconnected, scheduling reconnect");
		}
	} break;

	default:
		LOG_DBG("Unhandled WiFi event: 0x%08" PRIx32, mgmt_event);
		break;
	}
}

/* wpa supplicant events */
static void l3_wpa_supp_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
				      struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WPA_SUPP_READY:
		LOG_INF("[WPA-Supp] WPA Supplicant is ready");
		k_sem_give(&wpa_supplicant_ready_sem);
		break;
	case NET_EVENT_WPA_SUPP_NOT_READY:
		LOG_ERR("[WPA-Supp] WPA Supplicant is not ready");
		break;
	default:
		LOG_DBG("[WPA-Supp] Unhandled WPA Supplicant event: "
			"0x%08" PRIx32,
			mgmt_event);
		break;
	}
}

/* Enhanced network management event handler for L3 events */
static void l3_ipv4_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
				  struct net_if *iface)
{
	if ((mgmt_event & L3_IPV4_EVENT_MASK) != mgmt_event) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_IPV4_DHCP_START:
		LOG_INF("[DHCP] DHCP client started");
		break;

	case NET_EVENT_IPV4_DHCP_BOUND:
		LOG_INF("[DHCP] DHCP bound - IP address assigned");
		l3_dhcp_watchdog_cancel();
		network_connected = true;
		/* Print IP address information */
		wifi_print_dhcp_ip(iface, cb);
		/* Signal network connectivity */
		k_sem_give(&ipv4_dhcp_bond_sem);
		publish_network_ready(true);
		break;

	case NET_EVENT_IPV4_DHCP_STOP:
		LOG_WRN("[DHCP] DHCP client stopped");
		network_connected = false;
		break;

	case NET_EVENT_IPV4_ADDR_ADD:
		LOG_INF("[IPv4] IP address added to interface");
		/* Note: This fires AFTER DHCP_BOUND */
		break;

	case NET_EVENT_IPV4_ADDR_DEL:
		LOG_WRN("[IPv4] IP address removed from interface");
		network_connected = false;
		publish_network_ready(false);
		/* Lease lost while the link may still be associated - re-arm the
		 * watchdog to escape a "have link, no IP" zombie. Harmless if the
		 * link is actually down too: DISCONNECT_RESULT will cancel it, or
		 * on expiry NET_REQUEST_WIFI_DISCONNECT is a no-op when already
		 * disconnected.
		 */
		l3_dhcp_watchdog_arm();
		break;

	default:
		LOG_DBG("Unhandled IPv4 event: 0x%08" PRIx32, mgmt_event);
		break;
	}
}

bool net_event_mgmt_is_connected(void)
{
	return network_connected;
}

int init_network_events(void)
{
	LOG_INF("Initializing network event handlers");

	/* Initialize network event callbacks */
	net_mgmt_init_event_callback(&iface_event_cb, l2_iface_event_handler, L2_IF_EVENT_MASK);
	net_mgmt_add_event_callback(&iface_event_cb);
	LOG_DBG("Network interface event handler registered");

	/* Initialize and add the callback function for WiFi events (L2) */
	net_mgmt_init_event_callback(&wifi_event_cb, l2_wifi_conn_event_handler,
				     L2_WIFI_CONN_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_event_cb);
	LOG_DBG("WiFi L2 event handler registered");

	/* Initialize and add the callback function for WPA Supplicant events */
	net_mgmt_init_event_callback(&wpa_event_cb, l3_wpa_supp_event_handler,
				     L3_WPA_SUPP_EVENT_MASK);
	net_mgmt_add_event_callback(&wpa_event_cb);
	LOG_DBG("WPA Supplicant event handler registered");

	/* Initialize and add the callback function for network events (L3) */
	net_mgmt_init_event_callback(&ipv4_event_cb, l3_ipv4_event_handler, L3_IPV4_EVENT_MASK);
	net_mgmt_add_event_callback(&ipv4_event_cb);
	LOG_DBG("Network L3 event handler registered");

	k_work_queue_init(&net_connect_work_q);
	k_work_queue_start(&net_connect_work_q, net_connect_stack_area,
			   K_THREAD_STACK_SIZEOF(net_connect_stack_area), NET_CONNECT_WORKQ_PRIORITY,
			   &(const struct k_work_queue_config){.name = "net_connect_wq"});
	k_work_init_delayable(&wifi_connect_work, wifi_connect_work_handler);

	if (wifi_utils_has_stored_credentials()) {
		/* Delayed boot connect (1 s): gives BLE/Memfault/other subsystems
		 * time to finish their own SYS_INIT before attempting a WiFi
		 * connection using stored credentials.
		 */
		LOG_INF("WiFi credentials exist at boot, scheduling auto-connect");
		k_work_reschedule_for_queue(&net_connect_work_q, &wifi_connect_work, K_SECONDS(1));
	}

	LOG_INF("All network event handlers initialized successfully");

	return 0;
}

SYS_INIT(init_network_events, APPLICATION, CONFIG_WIFI_MODULE_INIT_PRIORITY);
