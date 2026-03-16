/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_MQTT_CLIENT_H
#define APP_MQTT_CLIENT_H

int app_mqtt_client_init(void);
void app_mqtt_client_notify_connected(void);
void app_mqtt_client_notify_disconnected(void);

#endif /* APP_MQTT_CLIENT_H */
