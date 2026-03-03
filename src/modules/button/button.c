/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "button.h"
#include "../messages.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(button_module, CONFIG_BUTTON_MODULE_LOG_LEVEL);

#include <dk_buttons_and_leds.h>
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/zbus/zbus.h>

#define BUTTON_COUNT 4

/* Map DK mask to button number (1-based) */
static const uint32_t button_masks[] = {
	DK_BTN1_MSK, DK_BTN2_MSK, DK_BTN3_MSK, DK_BTN4_MSK
};

/* ============================================================================
 * ZBUS CHANNEL DEFINITION
 * ============================================================================
 */

ZBUS_CHAN_DEFINE(BUTTON_CHAN, struct button_msg, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* ============================================================================
 * STATE MACHINE CONTEXT
 * ============================================================================
 */

static enum smf_state_result button_idle_run(void *obj);
static void button_pressed_entry(void *obj);
static enum smf_state_result button_pressed_run(void *obj);
static void button_released_entry(void *obj);

static const struct smf_state button_states[] = {
	[0] = SMF_CREATE_STATE(NULL, button_idle_run, NULL, NULL, NULL),
	[1] = SMF_CREATE_STATE(button_pressed_entry, button_pressed_run, NULL,
			       NULL, NULL),
	[2] = SMF_CREATE_STATE(button_released_entry, NULL, NULL, NULL, NULL),
};

struct button_sm_object {
	struct smf_ctx ctx;
	uint8_t button_number; /* 1-based */
	uint32_t press_count;
	int64_t press_timestamp_ms;
	bool current_state;
	bool previous_state;
};

static struct button_sm_object button_sm[BUTTON_COUNT];

/* ============================================================================
 * STATE MACHINE IMPLEMENTATIONS
 * ============================================================================
 */

static enum smf_state_result button_idle_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (sm->current_state && !sm->previous_state) {
		smf_set_state(SMF_CTX(sm), &button_states[1]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void button_pressed_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	sm->press_count++;
	sm->press_timestamp_ms = k_uptime_get();
}

static enum smf_state_result button_pressed_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (!sm->current_state && sm->previous_state) {
		smf_set_state(SMF_CTX(sm), &button_states[2]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void button_released_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;
	uint32_t duration_ms = (uint32_t)(k_uptime_get() - sm->press_timestamp_ms);

	struct button_msg msg = {
		.type = BUTTON_RELEASED,
		.button_number = sm->button_number,
		.duration_ms = duration_ms,
		.press_count = sm->press_count,
		.timestamp = k_uptime_get_32(),
	};
	zbus_chan_pub(&BUTTON_CHAN, &msg, K_MSEC(100));

	smf_set_state(SMF_CTX(sm), &button_states[0]);
}

/* ============================================================================
 * BUTTON EVENT HANDLING
 * ============================================================================
 */

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	for (int i = 0; i < BUTTON_COUNT; i++) {
		uint32_t mask = button_masks[i];

		if (has_changed & mask) {
			button_sm[i].current_state = (button_state & mask) ? true : false;
			int ret = smf_run_state(SMF_CTX(&button_sm[i]));
			if (ret < 0) {
				LOG_ERR("Button SM error: %d", ret);
			}
		}
	}
}

/* ============================================================================
 * MODULE INITIALIZATION
 * ============================================================================
 */

static int button_module_init(void)
{
	int ret;

	LOG_INF("Initializing button module");

	ret = dk_buttons_init(button_handler);
	if (ret) {
		LOG_ERR("Failed to initialize DK buttons: %d", ret);
		return ret;
	}

	for (int i = 0; i < BUTTON_COUNT; i++) {
		button_sm[i].button_number = (uint8_t)(i + 1);
		button_sm[i].press_count = 0;
		button_sm[i].press_timestamp_ms = 0;
		button_sm[i].current_state = false;
		button_sm[i].previous_state = false;
		smf_set_initial(SMF_CTX(&button_sm[i]), &button_states[0]);
	}

	LOG_INF("Button module initialized");
	return 0;
}

SYS_INIT(button_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
