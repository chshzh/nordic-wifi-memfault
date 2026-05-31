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
 * WIFI MESSAGES (STA mode)
 * ============================================================================
 */

enum wifi_msg_type {
	WIFI_STA_CONNECTED,
	WIFI_STA_DISCONNECTED,
	WIFI_DNS_READY,
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
