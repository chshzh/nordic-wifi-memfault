/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Memfault real-time timestamp provider via CLOCK_REALTIME.
 *
 * Only compiled when CONFIG_NTP_MODULE=y (nrf54lm20dk target). The NTP module
 * calls sys_clock_settime(SYS_CLOCK_REALTIME) after a successful SNTP sync.
 * This implementation reads that clock so Memfault events (crashes, traces,
 * metrics, CDR) carry wall-clock timestamps on the Memfault dashboard.
 *
 * Returns false until NTP has synced (epoch < 2020-01-01), causing Memfault
 * to mark events with unknown time rather than epoch-0 garbage.
 */

#include <time.h>
#include "memfault/core/platform/system_time.h"

/* 2020-01-01 00:00:00 UTC — any value below this means clock is unset */
#define MFLT_MIN_VALID_EPOCH 1577836800ULL

bool memfault_platform_time_get_current(sMemfaultCurrentTime *time)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		return false;
	}

	if ((uint64_t)ts.tv_sec < MFLT_MIN_VALID_EPOCH) {
		return false;
	}

	*time = (sMemfaultCurrentTime){
		.type = kMemfaultCurrentTimeType_UnixEpochTimeSec,
		.info =
			{
				.unix_timestamp_secs = (uint64_t)ts.tv_sec,
			},
	};
	return true;
}
