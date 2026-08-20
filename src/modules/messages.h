/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file messages.h
 * @brief Common zbus message definitions for Memfault nRF7002DK modules
 */

#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/* ============================================================================
 * BUTTON MESSAGES
 * ============================================================================
 */

enum button_msg_type {
	BUTTON_PRESSED,
	BUTTON_RELEASED,
};

struct button_msg {
	enum button_msg_type type;
	uint8_t button_number;
	uint32_t duration_ms;
	uint32_t press_count;
	uint32_t timestamp;
};

/* Long-press threshold (ms) - driven by CONFIG_BUTTON_LONG_PRESS_MS (Kconfig.button) */
#define BUTTON_LONG_PRESS_THRESHOLD_MS CONFIG_BUTTON_LONG_PRESS_MS

/* ============================================================================
 * WIFI MESSAGES (STA mode)
 * ============================================================================
 */

/* L2 (802.11 association) events only. IP-layer readiness is signaled
 * separately on NETWORK_CHAN (NETWORK_READY/NETWORK_NOT_READY below) --
 * subscribers that need "safe to do IP traffic now" should use that channel
 * instead of WIFI_STA_ASSOCIATED.
 */
enum wifi_msg_type {
	WIFI_STA_ASSOCIATED,
	WIFI_STA_DISCONNECTED,
	WIFI_ERROR,
};

struct wifi_msg {
	enum wifi_msg_type type;
	int32_t rssi;
	int error_code;
};

/* ============================================================================
 * NETWORK READY MESSAGES (DHCP/IP)
 * ============================================================================
 */

enum network_msg_type {
	NETWORK_READY,
	NETWORK_NOT_READY,
};

struct network_msg {
	enum network_msg_type type;
	bool ready;
};

#endif /* MESSAGES_H */
