/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ota_triggers.h"
#include "../../messages.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <memfault/nrfconnect_port/fota.h>

LOG_MODULE_REGISTER(ota_triggers, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

#define OTA_CHECK_INTERVAL K_MINUTES(CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN)

#define MFLT_OTA_TRIGGERS_THREAD_STACK_SIZE CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE
#define MFLT_OTA_TRIGGERS_THREAD_PRIORITY   K_LOWEST_APPLICATION_THREAD_PRIO

#define MFLT_OTA_TRIGGERS_BUTTON_FLAG  BIT(0)
#define MFLT_OTA_TRIGGERS_CONNECT_FLAG BIT(1)

static K_SEM_DEFINE(mflt_ota_triggers_sem, 0, 1);
static atomic_t mflt_ota_triggers_flags = ATOMIC_INIT(0);

static const char *consume_trigger_context(void)
{
	atomic_val_t flags = atomic_set(&mflt_ota_triggers_flags, 0);

	if (flags == 0) {
		return "manual";
	}
	if ((flags & MFLT_OTA_TRIGGERS_BUTTON_FLAG) &&
	    (flags & MFLT_OTA_TRIGGERS_CONNECT_FLAG)) {
		return "button+connect";
	}
	if (flags & MFLT_OTA_TRIGGERS_BUTTON_FLAG) {
		return "button";
	}
	if (flags & MFLT_OTA_TRIGGERS_CONNECT_FLAG) {
		return "connect";
	}
	return "manual";
}

static void schedule_ota_check(const char *context)
{
#if IS_ENABLED(CONFIG_MEMFAULT_FOTA)
	LOG_INF("Starting Memfault OTA check (%s)", context);
	int rv = memfault_fota_start();

	if (rv < 0) {
		LOG_ERR("Memfault OTA check failed (%s), err %d", context, rv);
	} else if (rv == 0) {
		LOG_INF("No new Memfault OTA update available (%s)", context);
	} else {
		LOG_INF("Memfault OTA download started (%s)", context);
	}
#else
	static bool warned;

	if (!warned) {
		LOG_WRN("Memfault OTA support is disabled. Enable "
			"CONFIG_MEMFAULT_FOTA to use OTA "
			"checks");
		warned = true;
	}
	ARG_UNUSED(context);
#endif
}

static void mflt_ota_triggers_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Memfault OTA trigger thread started");

	while (true) {
		int ret =
			k_sem_take(&mflt_ota_triggers_sem, OTA_CHECK_INTERVAL);
		k_sleep(K_SECONDS(10));

		if (ret == 0) {
			const char *context = consume_trigger_context();
			schedule_ota_check(context);
		} else if (ret == -EAGAIN) {
			schedule_ota_check("periodic");
		} else {
			LOG_ERR("k_sem_take returned unexpected value %d", ret);
		}
	}
}

K_THREAD_DEFINE(mflt_ota_triggers_tid, MFLT_OTA_TRIGGERS_THREAD_STACK_SIZE,
		mflt_ota_triggers_thread, NULL, NULL, NULL,
		MFLT_OTA_TRIGGERS_THREAD_PRIORITY, 0, 0);

void mflt_ota_triggers_notify_button(void)
{
	atomic_or(&mflt_ota_triggers_flags, MFLT_OTA_TRIGGERS_BUTTON_FLAG);

	if (k_sem_count_get(&mflt_ota_triggers_sem) == 0) {
		k_sem_give(&mflt_ota_triggers_sem);
		LOG_INF("Memfault OTA check requested by button press");
	} else {
		LOG_DBG("Memfault OTA check already pending");
	}
}

void mflt_ota_triggers_notify_connected(void)
{
	atomic_or(&mflt_ota_triggers_flags, MFLT_OTA_TRIGGERS_CONNECT_FLAG);

	if (k_sem_count_get(&mflt_ota_triggers_sem) == 0) {
		k_sem_give(&mflt_ota_triggers_sem);
		LOG_INF("Memfault OTA check scheduled for network connect");
	} else {
		LOG_DBG("Memfault OTA check already pending");
	}
}

/* Zbus: Button 2 short press -> OTA check; WIFI_STA_CONNECTED -> OTA on connect
 */
extern const struct zbus_channel BUTTON_CHAN;
extern const struct zbus_channel WIFI_CHAN;

static void ota_button_listener(const struct zbus_channel *chan)
{
	const struct button_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type != BUTTON_RELEASED || msg->button_number != 2) {
		return;
	}
	if (msg->duration_ms >= BUTTON_LONG_PRESS_THRESHOLD_MS) {
		return;
	}
	mflt_ota_triggers_notify_button();
}

static void ota_wifi_listener(const struct zbus_channel *chan)
{
	const struct wifi_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == WIFI_STA_CONNECTED) {
		mflt_ota_triggers_notify_connected();
	}
}

ZBUS_LISTENER_DEFINE(ota_button_listener_def, ota_button_listener);
ZBUS_LISTENER_DEFINE(ota_wifi_listener_def, ota_wifi_listener);

ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, ota_button_listener_def, 0);
ZBUS_CHAN_ADD_OBS(WIFI_CHAN, ota_wifi_listener_def, 0);
