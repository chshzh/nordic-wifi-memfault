/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NTP_H
#define NTP_H

/**
 * @file ntp.h
 * @brief NTP module — SNTP time synchronization.
 *
 * Queries CONFIG_NTP_MODULE_SERVER once Wi-Fi is connected and sets
 * CLOCK_REALTIME, so log output and application code (notably Memfault
 * event/log timestamps, including the FR-102 disconnect-time log-state
 * restore) can rely on real-world wall-clock time instead of device uptime.
 * Re-syncs periodically to compensate for crystal drift and retries
 * automatically on failure.
 *
 * Ported from zego/bricks/ntp: unlike the brick's decoupled
 * ZEGO_NTP_NET_CHAN + application weak-hook pattern, this module subscribes
 * directly to the existing WIFI_CHAN published by network/net_event_mgmt.c,
 * since this app already has that channel available.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the NTP sync module.
 *
 * Called automatically by SYS_INIT at APPLICATION priority.
 *
 * @return 0 on success, negative errno on failure.
 */
int ntp_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NTP_H */
