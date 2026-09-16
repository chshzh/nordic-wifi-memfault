/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_MEMFAULT_WIFI_METRICS_H
#define APP_MEMFAULT_WIFI_METRICS_H

void mflt_wifi_metrics_collect(void);

/* Report the current SSID/BSSID ("xx:xx:xx:xx:xx:xx" string). Called both from
 * the Wi-Fi connect event (e.g. zego_on_net_event_dhcp_bound()) and every
 * heartbeat (mflt_wifi_metrics_collect()) as a fallback for roams that don't
 * produce a fresh connect event. Records the values as metrics and bumps
 * wifi_ap_ssid_change_count / wifi_ap_bssid_change_count if they differ from
 * the previously reported values.
 */
void mflt_wifi_metrics_report_ssid_bssid(const char *ssid, const char *bssid);

#endif /* APP_MEMFAULT_WIFI_METRICS_H */
