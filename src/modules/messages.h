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
 * WIFI MESSAGES (STA mode) — types and WIFI_CHAN owned by zego/wifi_ble_prov.
 * Include wifi_ble_prov.h to get struct wifi_msg, enum wifi_msg_type, and
 * ZBUS_CHAN_DECLARE(WIFI_CHAN).
 * ============================================================================
 */
#if CONFIG_ZEGO_WIFI_BLE_PROV
#include <wifi_ble_prov.h>
#endif
enum network_msg_type {
	NETWORK_READY,
	NETWORK_NOT_READY,
};

struct network_msg {
	enum network_msg_type type;
	bool ready;
};

/* ============================================================================
 * APP WI-FI STATE — consumed by src/modules/ux/ for LED feedback.
 * Published by net_event_app.c on connectivity changes.
 * ============================================================================
 */
#include <wifi.h>

enum app_wifi_state {
	APP_WIFI_STATE_CONNECTING, /**< Boot default — not published explicitly */
	APP_WIFI_STATE_CONNECTED,  /**< STA connected or SoftAP client joined */
	APP_WIFI_STATE_ERROR,      /**< Disconnected / error */
};

struct app_wifi_state_msg {
	enum app_wifi_state state;
	enum zego_wifi_mode mode;
};

ZBUS_CHAN_DECLARE(APP_WIFI_STATE_CHAN);

#endif /* MESSAGES_H */
