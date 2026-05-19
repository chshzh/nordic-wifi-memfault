/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

/*
 * Platform overrides for the default configuration settings in the
 * memfault-firmware-sdk. Default configuration settings can be found in
 * "<NCS folder>/modules/lib/memfault-firmware-sdk/components/include/memfault/default_config.h"
 */

/* Enable MEMFAULT_LOG_RESTORE_STATE to compile memfault_log_get_state(), which
 * is required by memfault_log_state_restore.c at connect-time.
 * We do NOT override the weak memfault_log_restore_state() callback, so the
 * SDK default (returns false) fires harmlessly at the early-boot log backend
 * init — before Zephyr settings is ready.  The actual restore into the live
 * ring buffer happens in on_connect() via memfault_log_state_restore_on_connect().
 */
#if defined(CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE) && CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE
#define MEMFAULT_LOG_RESTORE_STATE 1
#else
#define MEMFAULT_LOG_RESTORE_STATE 0
#endif
