/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef WIFI_METRICS_H
#define WIFI_METRICS_H

void mflt_wifi_metrics_collect(void);

/* Report the current SSID/BSSID ("xx:xx:xx:xx:xx:xx" string). Called both from
 * the Wi-Fi connect event (wifi_print_status()) and every heartbeat
 * (mflt_wifi_metrics_collect()) as a fallback for roams that don't produce a
 * fresh connect event. Records the values as metrics and bumps
 * wifi_ssid_change_count / wifi_bssid_change_count if they differ from the
 * previously reported values.
 */
void mflt_wifi_metrics_report_ssid_bssid(const char *ssid, const char *bssid);

#endif /* WIFI_METRICS_H */
