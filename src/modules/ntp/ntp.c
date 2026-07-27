/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * NTP time synchronization module.
 *
 * Subscribes to WIFI_CHAN (published by network/net_event_mgmt.c). On
 * WIFI_STA_CONNECTED, queries the configured SNTP server and sets
 * CLOCK_REALTIME so log output and application code (notably Memfault
 * event/log timestamps) show real-world wall-clock time instead of device
 * uptime. Retries failed queries via a k_work_delayable item on the system
 * work queue -- no dedicated thread required. Resets on WIFI_STA_DISCONNECTED
 * so a fresh sync is performed after each reconnect.
 *
 * Ported from zego/bricks/ntp: unlike the brick's decoupled
 * ZEGO_NTP_NET_CHAN + application weak-hook pattern, this module subscribes
 * directly to this app's existing WIFI_CHAN.
 *
 * Memfault integration: this app does not enable CONFIG_DATE_TIME or
 * CONFIG_RTC, so the Memfault Zephyr port's MEMFAULT_SYSTEM_TIME_SOURCE
 * choice defaults to MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM, which has no
 * built-in memfault_platform_time_get_current() -- the SDK's weak default
 * always returns false (no time), so every Memfault event/log has so far
 * only ever gotten a server ingest-time timestamp, never a device
 * timestamp. This module supplies that missing implementation, backed by
 * the same CLOCK_REALTIME this module sets via SNTP.
 *
 * UART log timestamps: this Zephyr version (3.5.99) has no
 * CONFIG_LOG_TIMESTAMP_USE_REALTIME. Its logging core does, however, expose
 * log_set_timestamp_func() to swap the raw timestamp source at runtime, and
 * CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP to swap how that raw value is
 * printed. This module registers a CLOCK_REALTIME-backed timestamp source
 * once synced (1 Hz -- this project uses 32-bit log timestamps, so seconds,
 * not milliseconds, is the largest unit that fits an epoch value without
 * overflow), plus a custom formatter that renders it as a calendar UTC
 * string, e.g. "2026-07-24 14:35:33Z" instead of "[00:00:29.357,238]"
 * uptime or a raw epoch number.
 */

#include "ntp.h"
#include "../messages.h"

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/posix/time.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/init.h>

#if defined(CONFIG_APP_MEMFAULT_MODULE) && defined(CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM)
#include <memfault/core/platform/system_time.h>
#define NTP_MODULE_PROVIDES_MEMFAULT_TIME 1
#endif

LOG_MODULE_REGISTER(ntp_module, CONFIG_NTP_MODULE_LOG_LEVEL);

static bool ntp_synced;
static bool ntp_network_ready;

/* Tagged into bit 31 of the raw log_timestamp_t value by
 * ntp_log_timestamp_get() below, at the moment each log message is
 * *captured* (z_log_timestamp()), not when it is later formatted/printed.
 * With CONFIG_LOG_MODE_DEFERRED, those are different points in time -- a
 * message captured just before sync (uptime ms) could otherwise still be
 * sitting in the log buffer by the time ntp_synced flips true, and get
 * mis-rendered as epoch seconds (a live ntp_synced check in the formatter
 * was tried first and produced exactly that: bogus "1970-01-29" dates for
 * the last few pre-sync messages). Baking the mode into the value itself
 * makes formatting correct regardless of processing delay. Safe: today's
 * epoch seconds (~1.78e9) fit comfortably under this flag bit, and pre-sync
 * uptime-ms values would only collide with it after ~24.8 days of
 * continuous uptime before a first successful sync.
 */
#define NTP_LOG_TS_EPOCH_FLAG ((log_timestamp_t)0x80000000UL)

static log_timestamp_t ntp_log_timestamp_get(void)
{
	struct timespec tspec;

	if (clock_gettime(CLOCK_REALTIME, &tspec) != 0) {
		return 0;
	}
	return ((log_timestamp_t)tspec.tv_sec) | NTP_LOG_TS_EPOCH_FLAG;
}

/* Renders the raw timestamp with a "[...] " wrapper matching Zephyr's
 * built-in style. The NTP_LOG_TS_EPOCH_FLAG tag (set at capture time, see
 * above) says whether this particular message was captured post-sync
 * (render as a calendar UTC string) or pre-sync (render as an elapsed
 * hh:mm:ss.mmm uptime duration) -- this function replaces Zephyr's built-in
 * formatter entirely once CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP=y, so it
 * must handle both cases.
 */
static int ntp_log_timestamp_format(const struct log_output *output, const log_timestamp_t timestamp,
				    const log_timestamp_printer_t printer)
{
	if (!(timestamp & NTP_LOG_TS_EPOCH_FLAG)) {
		uint32_t ms = (uint32_t)timestamp;
		uint32_t s = ms / 1000U;
		uint32_t h = s / 3600U;
		uint32_t m = (s % 3600U) / 60U;

		s %= 60U;
		return printer(output, "[%02u:%02u:%02u.%03u] ", h, m, s, ms % 1000U);
	}

	time_t t = (time_t)(timestamp & ~NTP_LOG_TS_EPOCH_FLAG);
	struct tm tm_buf;
	struct tm *tm = gmtime_r(&t, &tm_buf);

	if (!tm) {
		return printer(output, "[%llu] ", (unsigned long long)t);
	}

	return printer(output, "[%04d-%02d-%02d %02d:%02d:%02dZ] ", tm->tm_year + 1900,
		       tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void ntp_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ntp_work, ntp_work_handler);

static void ntp_work_handler(struct k_work *work)
{
	struct sntp_time ts;
	struct timespec tspec;
	int ret;

	ARG_UNUSED(work);

	if (!ntp_network_ready) {
		return;
	}

	LOG_INF("Querying %s ...", CONFIG_NTP_MODULE_SERVER);

	ret = sntp_simple(CONFIG_NTP_MODULE_SERVER, CONFIG_NTP_MODULE_TIMEOUT_MS, &ts);
	if (ret < 0) {
		LOG_WRN("SNTP query failed (%d) - retry in %ds", ret,
			CONFIG_NTP_MODULE_RETRY_INTERVAL_SEC);
		k_work_reschedule(&ntp_work, K_SECONDS(CONFIG_NTP_MODULE_RETRY_INTERVAL_SEC));
		return;
	}

	tspec.tv_sec = (time_t)ts.seconds;
	tspec.tv_nsec = ((uint64_t)ts.fraction * NSEC_PER_SEC) >> 32;

	ret = clock_settime(CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("clock_settime failed (%d)", ret);
		return;
	}

	ntp_synced = true;
	LOG_INF("Time synced, epoch %llu (next resync in %d s)", ts.seconds,
		CONFIG_NTP_MODULE_RESYNC_INTERVAL_SEC);
	log_set_timestamp_func(ntp_log_timestamp_get, 1U);

	if (CONFIG_NTP_MODULE_RESYNC_INTERVAL_SEC > 0) {
		k_work_reschedule(&ntp_work, K_SECONDS(CONFIG_NTP_MODULE_RESYNC_INTERVAL_SEC));
	}
}

static void ntp_wifi_listener(const struct zbus_channel *chan)
{
	const struct wifi_msg *msg = zbus_chan_const_msg(chan);

	switch (msg->type) {
	case WIFI_STA_CONNECTED:
		ntp_network_ready = true;
		if (!ntp_synced) {
			k_work_reschedule(&ntp_work, K_NO_WAIT);
		}
		break;
	case WIFI_STA_DISCONNECTED:
		ntp_network_ready = false;
		ntp_synced = false;
		k_work_cancel_delayable(&ntp_work);
		break;
	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(ntp_wifi_listener_def, ntp_wifi_listener);

extern const struct zbus_channel WIFI_CHAN;
ZBUS_CHAN_ADD_OBS(WIFI_CHAN, ntp_wifi_listener_def, 0);

#if defined(NTP_MODULE_PROVIDES_MEMFAULT_TIME)
bool memfault_platform_time_get_current(sMemfaultCurrentTime *time)
{
	struct timespec tspec;

	if (!ntp_synced) {
		return false;
	}

	if (clock_gettime(CLOCK_REALTIME, &tspec) != 0) {
		return false;
	}

	*time = (sMemfaultCurrentTime){
		.type = kMemfaultCurrentTimeType_UnixEpochTimeSec,
		.info.unix_timestamp_secs = (uint64_t)tspec.tv_sec,
	};
	return true;
}
#endif /* NTP_MODULE_PROVIDES_MEMFAULT_TIME */

int ntp_module_init(void)
{
	log_custom_timestamp_set(ntp_log_timestamp_format);
	LOG_INF("NTP sync initialized (server: %s)", CONFIG_NTP_MODULE_SERVER);
	return 0;
}

static int ntp_module_sys_init(void)
{
	return ntp_module_init();
}
SYS_INIT(ntp_module_sys_init, APPLICATION, 3);
