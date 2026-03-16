/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_HTTPS_CLIENT_H
#define APP_HTTPS_CLIENT_H

int app_https_client_init(void);
void app_https_client_notify_connected(void);
void app_https_client_notify_disconnected(void);

#endif /* APP_HTTPS_CLIENT_H */
