/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Wi-Fi metrics helpers for Memfault heartbeat collection.
 */

#include "wifi_metrics.h"

#include <memfault/metrics/metrics.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(wifi_metrics, CONFIG_MEMFAULT_MODULE_LOG_LEVEL);

void mflt_wifi_metrics_collect(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = {0};

	if (!iface) {
		LOG_WRN("No network interface found");
		return;
	}

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		     sizeof(struct wifi_iface_status))) {
		LOG_WRN("Failed to get WiFi interface status");
		return;
	}

	if (status.state != WIFI_STATE_COMPLETED || status.iface_mode != WIFI_MODE_INFRA) {
		LOG_DBG("WiFi not connected in station mode, skipping metrics");
		return;
	}

	const char *link_mode_str =
		status.link_mode == WIFI_0    ? "802.11" :
		status.link_mode == WIFI_1    ? "802.11b" :
		status.link_mode == WIFI_2    ? "802.11a" :
		status.link_mode == WIFI_3    ? "802.11g" :
		status.link_mode == WIFI_4    ? "802.11n" :
		status.link_mode == WIFI_5    ? "802.11ac" :
		status.link_mode == WIFI_6    ? "802.11ax" :
		status.link_mode == WIFI_6E   ? "802.11ax/6GHz" :
		status.link_mode == WIFI_7    ? "802.11be" :
		"unknown";
	MEMFAULT_METRIC_SET_STRING(wifi_standard_version, link_mode_str);

	const char *security_str =
		status.security == WIFI_SECURITY_TYPE_NONE ? "NONE" :
		status.security == WIFI_SECURITY_TYPE_PSK ? "WPA2-PSK" :
		status.security == WIFI_SECURITY_TYPE_PSK_SHA256 ? "WPA2-PSK-SHA256" :
		status.security == WIFI_SECURITY_TYPE_SAE ? "WPA3-SAE" :
		status.security == WIFI_SECURITY_TYPE_WPA_PSK ? "WPA-PSK" :
		status.security == WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL ? "WPA-AUTO-PERSONAL" :
		"UNKNOWN";
	MEMFAULT_METRIC_SET_STRING(wifi_security_type, security_str);

	const char *band_str =
		status.band == WIFI_FREQ_BAND_2_4_GHZ ? "2.4" :
		status.band == WIFI_FREQ_BAND_5_GHZ ? "5" :
		status.band == WIFI_FREQ_BAND_6_GHZ ? "6" :
		"x";
	MEMFAULT_METRIC_SET_STRING(wifi_frequency_band, band_str);

	char oui[9];
	snprintf(oui, sizeof(oui), "%02x:%02x:%02x",
		 status.bssid[0], status.bssid[1], status.bssid[2]);
	MEMFAULT_METRIC_SET_STRING(wifi_ap_oui, oui);

	MEMFAULT_METRIC_SET_UNSIGNED(wifi_primary_channel, status.channel);
	MEMFAULT_METRIC_SET_SIGNED(wifi_sta_rssi, status.rssi);
	MEMFAULT_METRIC_SET_UNSIGNED(wifi_beacon_interval, status.beacon_interval);
	MEMFAULT_METRIC_SET_UNSIGNED(wifi_dtim_interval, status.dtim_period);
	MEMFAULT_METRIC_SET_UNSIGNED(wifi_twt_capable, status.twt_capable);

	if (status.current_phy_tx_rate > 0.0f) {
		MEMFAULT_METRIC_SET_UNSIGNED(wifi_tx_rate_mbps,
					     (uint32_t)status.current_phy_tx_rate);
	}

	LOG_INF("WiFi metrics collected: RSSI=%d dBm, Channel=%u, TX rate=%.1f Mbps, OUI=%s",
		status.rssi, status.channel, (double)status.current_phy_tx_rate, oui);
}
