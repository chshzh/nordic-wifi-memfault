/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4_server.h>
#include <supp_events.h>
#include <zephyr/net/socket.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#include <zephyr/zbus/zbus.h>

#include "../messages.h"

#include "net_event_mgmt.h"
#include "wifi_utils.h"

LOG_MODULE_REGISTER(net_event_mgmt, CONFIG_WIFI_MODULE_LOG_LEVEL);

ZBUS_CHAN_DEFINE(WIFI_CHAN, struct wifi_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(NETWORK_CHAN, struct network_msg, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

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
#define L2_IF_EVENT_MASK ((uint64_t)(NET_EVENT_IF_DOWN | NET_EVENT_IF_UP))
#define L2_WIFI_CONN_EVENT_MASK                                                \
	((uint64_t)(NET_EVENT_WIFI_CONNECT_RESULT |                            \
		    NET_EVENT_WIFI_DISCONNECT_RESULT))
#define L2_WIFI_SOFTAP_EVENT_MASK                                              \
	((uint64_t)(NET_EVENT_WIFI_AP_ENABLE_RESULT |                          \
		    NET_EVENT_WIFI_AP_STA_CONNECTED |                          \
		    NET_EVENT_WIFI_AP_STA_DISCONNECTED))
#define L3_WPA_SUPP_EVENT_MASK                                                 \
	((uint64_t)(NET_EVENT_SUPPLICANT_READY |                               \
		    NET_EVENT_SUPPLICANT_NOT_READY))
/* Comprehensive IPv4/DHCP event mask for complete lifecycle tracking */
#define L3_IPV4_EVENT_MASK                                                     \
	((uint64_t)(NET_EVENT_IPV4_DHCP_START | NET_EVENT_IPV4_DHCP_BOUND |    \
		    NET_EVENT_IPV4_DHCP_STOP | NET_EVENT_IPV4_ADDR_ADD |       \
		    NET_EVENT_IPV4_ADDR_DEL))

/* Define network event semaphores */
K_SEM_DEFINE(iface_up_sem, 0, 1);
K_SEM_DEFINE(wpa_supplicant_ready_sem, 0, 1);
K_SEM_DEFINE(ipv4_dhcp_bond_sem, 0, 1);

/* Track network connectivity state (WiFi connected + IP assigned) */
static bool network_connected;
/* Function declarations */

/* Declare the callback structures for Wi-Fi and network events */
static struct net_mgmt_event_callback iface_event_cb;
static struct net_mgmt_event_callback wpa_event_cb;
static struct net_mgmt_event_callback wifi_event_cb;
static struct net_mgmt_event_callback ipv4_event_cb;
#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
static struct net_mgmt_event_callback softap_event_cb;

/* SoftAP support */
static K_MUTEX_DEFINE(softap_mutex);
K_SEM_DEFINE(station_connected_sem, 0,
	     1); /* Semaphore to wait for station connection */

struct softap_station {
	bool valid;
	struct wifi_ap_sta_info info;
	struct in_addr ip_addr; /* Store station's IP address */
};

#define MAX_SOFTAP_STATIONS 4
static struct softap_station connected_stations[MAX_SOFTAP_STATIONS];
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_AP */

static void l2_iface_event_handler(struct net_mgmt_event_callback *cb,
				   uint64_t mgmt_event, struct net_if *iface)
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
		LOG_DBG("[IF] Unhandled network event: 0x%08" PRIx64,
			mgmt_event);
		break;
	}
}

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)

static int get_station_ip_address(const uint8_t *mac, struct in_addr *ip_addr)
{
	struct net_if *iface;
	int station_count = 0;

	iface = net_if_get_first_wifi();
	if (!iface) {
		return -1;
	}

	/* Count existing stations to assign next sequential IP */
	k_mutex_lock(&softap_mutex, K_FOREVER);
	for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
		if (connected_stations[i].valid &&
		    connected_stations[i].ip_addr.s_addr != 0) {
			station_count++;
		}
	}
	k_mutex_unlock(&softap_mutex);

	/* Assign IP based on DHCP pool logic: 192.168.1.2, 192.168.1.3, etc. */
	/* DHCP server starts from 192.168.1.2, so assign sequentially */
	uint32_t base_ip =
		0xC0A80102; /* 192.168.1.2 in network byte order (host order) */
	uint32_t assigned_ip = base_ip + station_count;

	ip_addr->s_addr = htonl(assigned_ip);

	LOG_DBG("Assigned IP for station: 192.168.1.%d (station count: %d)",
		2 + station_count, station_count + 1);

	return 0;
}

static void handle_softap_enable_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (status->status) {
		LOG_ERR("SoftAP enable failed: %d", status->status);
	} else {
		LOG_INF("SoftAP enabled successfully");
		/* Signal network connectivity for SoftAP mode */
		k_sem_give(&ipv4_dhcp_bond_sem);
	}
}

static void handle_station_connected(struct net_mgmt_event_callback *cb)
{
	const struct wifi_ap_sta_info *sta_info =
		(const struct wifi_ap_sta_info *)cb->info;
	int station_slot = -1;

	k_mutex_lock(&softap_mutex, K_FOREVER);

	/* Find empty slot for new station */
	for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
		if (!connected_stations[i].valid) {
			connected_stations[i].valid = true;
			connected_stations[i].info = *sta_info;
			connected_stations[i].ip_addr.s_addr =
				0; /* Initialize as unassigned */
			station_slot = i;
			break;
		}
	}

	k_mutex_unlock(&softap_mutex);

	uint8_t mac_str[18];
	snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
		 sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
		 sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);

	/* Try to get IP address after a short delay for DHCP assignment */
	k_sleep(K_MSEC(1000)); /* Wait 1 second for DHCP to assign IP */

	if (station_slot >= 0) {
		struct in_addr ip_addr;
		if (get_station_ip_address(sta_info->mac, &ip_addr) == 0) {
			k_mutex_lock(&softap_mutex, K_FOREVER);
			connected_stations[station_slot].ip_addr = ip_addr;
			k_mutex_unlock(&softap_mutex);

			char ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str));
			LOG_INF("Station %s assigned IP: %s", mac_str, ip_str);
		} else {
			LOG_WRN("Could not determine IP address for station %s",
				mac_str);
		}
	}

	/* Signal that a station has connected */
	k_sem_give(&station_connected_sem);
	LOG_INF("New device connected with AP!");
}

static void handle_station_disconnected(struct net_mgmt_event_callback *cb)
{
	const struct wifi_ap_sta_info *sta_info =
		(const struct wifi_ap_sta_info *)cb->info;
	char mac_str[18];
	char ip_str[INET_ADDRSTRLEN] = "Unknown";

	snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
		 sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
		 sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);

	k_mutex_lock(&softap_mutex, K_FOREVER);

	/* Find and remove station from list, also get its IP address */
	for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
		if (connected_stations[i].valid &&
		    memcmp(connected_stations[i].info.mac, sta_info->mac, 6) ==
			    0) {

			/* Get IP address before removing */
			if (connected_stations[i].ip_addr.s_addr != 0) {
				inet_ntop(AF_INET,
					  &connected_stations[i].ip_addr,
					  ip_str, sizeof(ip_str));
			}

			connected_stations[i].valid = false;
			connected_stations[i].ip_addr.s_addr = 0;
			break;
		}
	}

	k_mutex_unlock(&softap_mutex);

	LOG_INF("Station disconnected: MAC=%s, IP=%s", mac_str, ip_str);

	/* Check if any stations are still connected */
	k_mutex_lock(&softap_mutex, K_FOREVER);
	bool any_connected = false;
	for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
		if (connected_stations[i].valid) {
			any_connected = true;
			break;
		}
	}
	k_mutex_unlock(&softap_mutex);

	if (!any_connected) {
		LOG_INF("No stations remaining connected to SoftAP");
	}
}

static void l2_wifi_softap_event_handler(struct net_mgmt_event_callback *cb,
					 uint64_t mgmt_event,
					 struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		handle_softap_enable_result(cb);
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED:
		handle_station_connected(cb);
		break;
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED:
		handle_station_disconnected(cb);
		break;
	default:
		break;
	}
}
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_AP */

/* Enhanced WiFi management event handler for L2 events */
static void l2_wifi_conn_event_handler(struct net_mgmt_event_callback *cb,
				       uint64_t mgmt_event,
				       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status =
			(const struct wifi_status *)cb->info;

		if (status->status == 0) {
			/* Connection successful */
			LOG_INF("[WiFi] WiFi is connected!");
			/* Print detailed WiFi status when connected */
			wifi_print_status();
		} else {
			/* Decode common error codes */
			switch (status->status) {
			case 1:
				LOG_ERR("[WiFi] Reason: Generic failure");
				break;
			case 2:
				LOG_ERR("[WiFi] Reason: Authentication "
					"timeout");
				break;
			case 3:
				LOG_ERR("[WiFi] Reason: Authentication failed");
				break;
			case 15:
				LOG_ERR("[WiFi] Reason: AP not found");
				break;
			case 16:
				LOG_ERR("[WiFi] Reason: Association timeout");
				break;
			case -ETIMEDOUT:
				LOG_ERR("[WiFi] Reason: Connection timed out "
					"(-ETIMEDOUT), please check "
					"your WiFi credentials or if the AP is "
					"available");
				break;
			default:
				LOG_ERR("[WiFi] Reason: Unknown error code %d",
					status->status);
				break;
			}

			publish_wifi_event(WIFI_ERROR, status->status);
		}
	} break;

	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		const struct wifi_status *status =
			(const struct wifi_status *)cb->info;

		if (status) {
			LOG_ERR("=== WiFi DISCONNECTED ===");
			LOG_ERR("[WiFi] Status code: %d", status->status);

			/* Common disconnect reasons (from WiFi spec) */
			switch (status->status) {
			case 0:
				LOG_ERR("[WiFi] Reason: Success (intentional "
					"disconnect)");
				break;
			case 1:
				LOG_ERR("[WiFi] Reason: Unspecified");
				break;
			case 2:
				LOG_ERR("[WiFi] Reason: Previous auth no "
					"longer "
					"valid");
				break;
			case 3:
				LOG_ERR("[WiFi] Reason: Deauth - leaving "
					"network");
				break;
			case 4:
				LOG_ERR("[WiFi] Reason: Disassoc - inactivity "
					"timeout");
				break;
			case 6:
				LOG_ERR("[WiFi] Reason: Class 2 frame from "
					"non-auth "
					"STA");
				break;
			case 7:
				LOG_ERR("[WiFi] Reason: Class 3 frame from "
					"non-assoc "
					"STA");
				break;
			case 8:
				LOG_ERR("[WiFi] Reason: Disassoc - STA "
					"leaving");
				break;
			case 15:
				LOG_ERR("[WiFi] Reason: 4-way handshake "
					"timeout");
				break;
			case 16:
				LOG_ERR("[WiFi] Reason: Group key handshake "
					"timeout");
				break;
			case 23:
				LOG_ERR("[WiFi] Reason: IEEE 802.1X auth "
					"failed");
				break;
			default:
				LOG_ERR("[WiFi] Reason: Unknown (%d)",
					status->status);
				break;
			}
			LOG_ERR("=========================");
		} else {
			LOG_ERR("[WiFi] WiFi disconnected: status=NULL");
		}

		network_connected = false;
		publish_wifi_event(WIFI_STA_DISCONNECTED,
				   status ? status->status : -1);
		publish_network_ready(false);
	} break;

	default:
		LOG_DBG("Unhandled WiFi event: 0x%08" PRIx64, mgmt_event);
		break;
	}
}

/* wpa supplicant events */
static void l3_wpa_supp_event_handler(struct net_mgmt_event_callback *cb,
				      uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_SUPPLICANT_READY:
		LOG_INF("[WPA-Supp] WPA Supplicant is ready");
		k_sem_give(&wpa_supplicant_ready_sem);
		break;
	case NET_EVENT_SUPPLICANT_NOT_READY:
		LOG_ERR("[WPA-Supp] WPA Supplicant is not ready");
		break;
	default:
		LOG_DBG("[WPA-Supp] Unhandled WPA Supplicant event: "
			"0x%08" PRIx64,
			mgmt_event);
		break;
	}
}

/* Enhanced network management event handler for L3 events */
static void l3_ipv4_event_handler(struct net_mgmt_event_callback *cb,
				  uint64_t mgmt_event, struct net_if *iface)
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
		network_connected = true;
		/* Print IP address information */
		wifi_print_dhcp_ip(iface, cb);
		/* Signal network connectivity */
		k_sem_give(&ipv4_dhcp_bond_sem);
		publish_wifi_event(WIFI_STA_CONNECTED, 0);
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
		break;

	default:
		LOG_DBG("Unhandled IPv4 event: 0x%08" PRIx64, mgmt_event);
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
	net_mgmt_init_event_callback(&iface_event_cb, l2_iface_event_handler,
				     L2_IF_EVENT_MASK);
	net_mgmt_add_event_callback(&iface_event_cb);
	LOG_DBG("Network interface event handler registered");

	/* Initialize and add the callback function for WiFi events (L2) */
	net_mgmt_init_event_callback(&wifi_event_cb, l2_wifi_conn_event_handler,
				     L2_WIFI_CONN_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_event_cb);
	LOG_DBG("WiFi L2 event handler registered");

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
	/* Initialize SoftAP event callbacks */
	net_mgmt_init_event_callback(&softap_event_cb,
				     l2_wifi_softap_event_handler,
				     L2_WIFI_SOFTAP_EVENT_MASK);
	net_mgmt_add_event_callback(&softap_event_cb);
	LOG_DBG("SoftAP event handler registered");
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_AP */

	/* Initialize and add the callback function for WPA Supplicant events */
	net_mgmt_init_event_callback(&wpa_event_cb, l3_wpa_supp_event_handler,
				     L3_WPA_SUPP_EVENT_MASK);
	net_mgmt_add_event_callback(&wpa_event_cb);
	LOG_DBG("WPA Supplicant event handler registered");

	/* Initialize and add the callback function for network events (L3) */
	net_mgmt_init_event_callback(&ipv4_event_cb, l3_ipv4_event_handler,
				     L3_IPV4_EVENT_MASK);
	net_mgmt_add_event_callback(&ipv4_event_cb);
	LOG_DBG("Network L3 event handler registered");

	LOG_INF("All network event handlers initialized successfully");

	return 0;
}

SYS_INIT(init_network_events, APPLICATION, CONFIG_WIFI_MODULE_INIT_PRIORITY);
