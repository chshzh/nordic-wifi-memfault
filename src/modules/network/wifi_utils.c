/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <net/wifi_mgmt_ext.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#include "wifi_utils.h"

LOG_MODULE_REGISTER(wifi_utils, CONFIG_WIFI_MODULE_LOG_LEVEL);

static char last_connected_ssid[WIFI_SSID_MAX_LEN + 1];

const char *wifi_utils_get_last_ssid(void)
{
	if (last_connected_ssid[0] == '\0') {
		return NULL;
	}

	return last_connected_ssid;
}

int wifi_utils_auto_connect_stored(void)
{

#if !defined(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED)
	return -ENOTSUP;
#else
	struct net_if *iface = net_if_get_first_wifi();
	int ret;

	if (iface == NULL) {
		return -ENODEV;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
	if (ret == 0) {
		LOG_INF("Auto-connect request issued for stored Wi-Fi "
			"credentials");
	} else if (ret != -EALREADY) {
		LOG_WRN("Auto-connect request failed: %d", ret);
	}

	return ret;
#endif
}

int wifi_set_mode(int mode)
{
	struct net_if *iface;
	struct wifi_mode_info mode_info = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return -ENODEV;
	}

	mode_info.oper = WIFI_MGMT_SET;
	mode_info.if_index = net_if_get_by_iface(iface);
	mode_info.mode = mode;

	ret = net_mgmt(NET_REQUEST_WIFI_MODE, iface, &mode_info, sizeof(mode_info));
	if (ret) {
		LOG_ERR("Mode setting failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi mode set to %d", mode);
	return 0;
}

int wifi_set_channel(int channel)
{
	struct net_if *iface;
	struct wifi_channel_info channel_info = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return -ENODEV;
	}

	channel_info.oper = WIFI_MGMT_SET;
	channel_info.if_index = net_if_get_by_iface(iface);
	channel_info.channel = channel;

	if ((channel_info.channel < WIFI_CHANNEL_MIN) ||
	    (channel_info.channel > WIFI_CHANNEL_MAX)) {
		LOG_ERR("Invalid channel number: %d. Range is (%d-%d)", channel, WIFI_CHANNEL_MIN,
			WIFI_CHANNEL_MAX);
		return -EINVAL;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_CHANNEL, iface, &channel_info, sizeof(channel_info));
	if (ret) {
		LOG_ERR("Channel setting failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi channel set to %d", channel_info.channel);
	return 0;
}

int wifi_set_tx_injection_mode(void)
{
	struct net_if *iface;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return -ENODEV;
	}

	if (net_eth_txinjection_mode(iface, true)) {
		LOG_ERR("TX Injection mode enable failed");
		return -1;
	}

	LOG_INF("TX Injection mode enabled");
	return 0;
}

int wifi_print_status(void)
{
	struct net_if *iface;
	struct wifi_iface_status status = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi interface");
		return -ENODEV;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		       sizeof(struct wifi_iface_status));
	if (ret) {
		LOG_ERR("Status request failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi Status: successful");
	LOG_INF("==================");
	LOG_INF("State: %s", wifi_state_txt(status.state));

	if (status.state >= WIFI_STATE_ASSOCIATED) {
		strncpy(last_connected_ssid, status.ssid, WIFI_SSID_MAX_LEN);
		last_connected_ssid[WIFI_SSID_MAX_LEN] = '\0';
		LOG_INF("Interface Mode: %s", wifi_mode_txt(status.iface_mode));
		LOG_INF("SSID: %.32s", status.ssid);
		LOG_INF("BSSID: %02x:%02x:%02x:%02x:%02x:%02x", status.bssid[0], status.bssid[1],
			status.bssid[2], status.bssid[3], status.bssid[4], status.bssid[5]);
		LOG_INF("Band: %s", wifi_band_txt(status.band));
		LOG_INF("Channel: %d", status.channel);
		LOG_INF("Security: %s", wifi_security_txt(status.security));
		LOG_INF("RSSI: %d dBm", status.rssi);
	} else {
		last_connected_ssid[0] = '\0';
	}

	return 0;
}

#if defined(CONFIG_NET_DHCPV4)
void wifi_print_dhcp_ip(struct net_if *iface, struct net_mgmt_event_callback *cb)
{
	const struct net_if_dhcpv4 *dhcpv4 = cb->info;
	const struct in_addr *addr = &dhcpv4->requested_ip;
	char dhcp_info[128];
	char netmask_info[128];
	char gw_info[128];

	net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));

	/* net_if_ipv4_get_netmask_by_addr()/net_if_ipv4_get_gw() do not exist
	 * on the Zephyr version bundled with NCS v2.6.4 (single-address-per-
	 * interface IPv4 API); use the single-netmask getter and read the
	 * gateway directly off the interface's IPv4 config.
	 */
	struct in_addr netmask = net_if_ipv4_get_netmask(iface);
	net_addr_ntop(AF_INET, &netmask, netmask_info, sizeof(netmask_info));

	struct in_addr gw = iface->config.ip.ipv4 ? iface->config.ip.ipv4->gw : (struct in_addr){0};
	net_addr_ntop(AF_INET, &gw, gw_info, sizeof(gw_info));

	LOG_INF("\r\n\r\nDevice IP address: %s\r\nSubnet mask: %s\r\nGateway: %s\r\n", dhcp_info,
		netmask_info, gw_info);
}
#else
void wifi_print_dhcp_ip(struct net_if *iface, struct net_mgmt_event_callback *cb)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);
}
#endif
