/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_mgmt.h>

/**
 * @brief Print detailed Wi-Fi status information
 *
 * @return 0 on success, negative error code on failure
 */
int wifi_print_status(void);

/**
 * @brief Print DHCP IP address when bound
 *
 * @param iface Pointer to the network interface
 * @param cb Network management event callback containing DHCP info
 */
void wifi_print_dhcp_ip(struct net_if *iface, struct net_mgmt_event_callback *cb);

/**
 * @brief Get the last connected SSID string
 *
 * @return Pointer to the last SSID stored, or NULL if no SSID is available
 */
const char *wifi_utils_get_last_ssid(void);

/**
 * @brief Check whether any Wi-Fi credentials are stored.
 *
 * @return true if at least one SSID is stored in the wifi_credentials backend
 */
bool wifi_utils_has_stored_credentials(void);

/**
 * @brief Set Wi-Fi channel for raw packet operations
 *
 * @param channel Channel number to set
 * @return 0 on success, negative error code on failure
 */
int wifi_set_channel(int channel);

/**
 * @brief Set Wi-Fi mode
 *
 * @param mode Mode value to set
 * @return 0 on success, negative error code on failure
 */
int wifi_set_mode(int mode);

/**
 * @brief Enable TX injection mode
 *
 * @return 0 on success, negative error code on failure
 */
int wifi_set_tx_injection_mode(void);

#endif /* WIFI_UTILS_H */
