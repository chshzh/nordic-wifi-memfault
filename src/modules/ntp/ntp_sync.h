/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NTP_SYNC_H
#define NTP_SYNC_H

/**
 * @brief Initialize the NTP sync module.
 *
 * Called automatically by SYS_INIT at APPLICATION priority.
 * Sets up the NETWORK_CHAN zbus listener and logs the configured server.
 *
 * @return 0 on success, negative errno on failure.
 */
int ntp_sync_init(void);

#endif /* NTP_SYNC_H */
