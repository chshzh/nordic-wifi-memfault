/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file ux.c
 * @brief LED Wi-Fi state feedback for nordic-wifi-memfault.
 *
 * Subscribes to APP_WIFI_STATE_CHAN and drives LED 0:
 *   CONNECTING  →  MARQUEE  (started at boot via SYS_INIT)
 *   CONNECTED   →  Solid ON
 *   ERROR       →  Fast BLINK (100 ms half-period)
 *
 * No button handling — this is a LED-only UX module.
 * Button events (heartbeat/OTA/crash-demo) are handled directly in
 * src/modules/app_memfault/core/app_memfault_core.c via BUTTON_CHAN.
 *
 * Inputs:  APP_WIFI_STATE_CHAN
 * Outputs: LED_CMD_CHAN
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "led.h"
#include "../messages.h"

LOG_MODULE_REGISTER(app_ux, LOG_LEVEL_INF);

/* ── LED helpers ───────────────────────────────────────────────────────── */

static void led_set(enum led_msg_type type, uint16_t period_ms)
{
	struct led_msg msg = {
		.type = type,
		.led_number = 0,
		.period_ms = period_ms,
	};

	zbus_chan_pub(&LED_CMD_CHAN, &msg, K_NO_WAIT);
}

static void apply_wifi_state_led(enum app_wifi_state state)
{
	switch (state) {
	case APP_WIFI_STATE_CONNECTING:
		led_set(LED_COMMAND_MARQUEE, 0);
		break;
	case APP_WIFI_STATE_CONNECTED:
		led_set(LED_COMMAND_ON, 0);
		break;
	case APP_WIFI_STATE_ERROR:
		led_set(LED_COMMAND_BLINK, 100);
		break;
	}
}

/* ── Deferred LED work ─────────────────────────────────────────────────── */

static enum app_wifi_state last_wifi_state = APP_WIFI_STATE_CONNECTING;
static enum app_wifi_state pending_wifi_state;

/*
 * Ready flag: set to 1 in app_ux_init() once the LED module (SYS_INIT
 * priority 91) is fully initialised.  The network event thread can submit
 * app_ux_led_work before SYS_INIT reaches the LED module; gating on this
 * flag prevents k_work_schedule() from inserting a garbage timeout node
 * before k_work_init_delayable() has run.
 */
static atomic_t app_ux_ready = ATOMIC_INIT(0);

static void app_ux_led_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_get(&app_ux_ready)) {
		/* LED module not yet initialised — will be replayed in app_ux_init */
		return;
	}
	apply_wifi_state_led(pending_wifi_state);
}

static K_WORK_DEFINE(app_ux_led_work, app_ux_led_work_fn);

/* ── APP_WIFI_STATE_CHAN listener ──────────────────────────────────────── */

static void wifi_state_listener_cb(const struct zbus_channel *chan)
{
	const struct app_wifi_state_msg *msg = zbus_chan_const_msg(chan);

	last_wifi_state = msg->state;
	pending_wifi_state = msg->state;
	k_work_submit(&app_ux_led_work);
}

ZBUS_LISTENER_DEFINE(app_wifi_state_listener, wifi_state_listener_cb);
ZBUS_CHAN_ADD_OBS(APP_WIFI_STATE_CHAN, app_wifi_state_listener, 0);

/* ── SYS_INIT: start MARQUEE at boot ───────────────────────────────────── */

static int app_ux_init(void)
{
	/* Start boot animation — LED module (priority 91) has already run. */
	led_set(LED_COMMAND_MARQUEE, 0);

	/* Unblock app_ux_led_work_fn */
	atomic_set(&app_ux_ready, 1);

	/* Replay any state that arrived before init (e.g. early NETWORK_READY). */
	if (last_wifi_state != APP_WIFI_STATE_CONNECTING) {
		pending_wifi_state = last_wifi_state;
		k_work_submit(&app_ux_led_work);
	}

	return 0;
}

SYS_INIT(app_ux_init, APPLICATION, CONFIG_APP_UX_INIT_PRIORITY);
