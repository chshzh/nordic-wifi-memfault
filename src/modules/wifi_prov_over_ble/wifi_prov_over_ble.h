/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef WIFI_PROV_OVER_BLE_H
#define WIFI_PROV_OVER_BLE_H

#include <stdbool.h>

int wifi_prov_over_ble_init(void);
void wifi_prov_over_ble_update_wifi_status(bool connected);

#endif /* WIFI_PROV_OVER_BLE_H */
