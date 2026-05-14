/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * NTP time synchronization module.
 *
 * Subscribes to NETWORK_CHAN. On NETWORK_READY, queries the configured SNTP
 * server and sets CLOCK_REALTIME so Zephyr log output shows real-world
 * wall-clock timestamps when CONFIG_LOG_TIMESTAMP_USE_REALTIME=y.
 *
 * Retries failed queries via a k_work_delayable item on the system work queue
 * — no dedicated thread required. Resets on NETWORK_NOT_READY so a fresh sync
 * is performed after each reconnect.
 */

#include "ntp_sync.h"
#include "../messages.h"

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/init.h>

LOG_MODULE_REGISTER(ntp_sync, CONFIG_NTP_MODULE_LOG_LEVEL);

static bool ntp_synced;
static bool ntp_network_ready;

static void ntp_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(ntp_work, ntp_work_handler);

static void ntp_work_handler(struct k_work *work)
{
	struct sntp_time ts;
	struct timespec tspec;
	int ret;

	if (!ntp_network_ready) {
		return;
	}

	LOG_INF("Querying %s ...", CONFIG_NTP_SERVER);

	ret = sntp_simple(CONFIG_NTP_SERVER, CONFIG_NTP_TIMEOUT_MS, &ts);
	if (ret < 0) {
		LOG_WRN("SNTP query failed (%d) — retry in %ds",
			ret, CONFIG_NTP_RETRY_INTERVAL_SEC);
		k_work_reschedule(&ntp_work,
				  K_SECONDS(CONFIG_NTP_RETRY_INTERVAL_SEC));
		return;
	}

	tspec.tv_sec = (time_t)ts.seconds;
	tspec.tv_nsec = ((uint64_t)ts.fraction * NSEC_PER_SEC) >> 32;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("sys_clock_settime failed (%d)", ret);
		return;
	}

	ntp_synced = true;
	LOG_INF("Time synced — epoch %llu", ts.seconds);
}

static void ntp_notify_connected(void)
{
	ntp_network_ready = true;
	if (!ntp_synced) {
		k_work_reschedule(&ntp_work, K_NO_WAIT);
	}
}

static void ntp_notify_disconnected(void)
{
	ntp_network_ready = false;
	ntp_synced = false;
	k_work_cancel_delayable(&ntp_work);
}

extern const struct zbus_channel NETWORK_CHAN;

static void ntp_network_cb(const struct zbus_channel *chan)
{
	const struct network_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == NETWORK_READY) {
		ntp_notify_connected();
	} else if (msg->type == NETWORK_NOT_READY) {
		ntp_notify_disconnected();
	}
}

ZBUS_LISTENER_DEFINE(ntp_network_listener, ntp_network_cb);
ZBUS_CHAN_ADD_OBS(NETWORK_CHAN, ntp_network_listener, 0);

int ntp_sync_init(void)
{
	LOG_INF("NTP sync initialized (server: %s)", CONFIG_NTP_SERVER);
	return 0;
}

static int ntp_sync_module_init(void)
{
	return ntp_sync_init();
}
SYS_INIT(ntp_sync_module_init, APPLICATION, 3);
