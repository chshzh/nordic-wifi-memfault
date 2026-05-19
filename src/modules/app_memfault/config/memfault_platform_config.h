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

/* Ring-buffer state restore is performed at WiFi connect-time (not at boot) to
 * avoid a Zephyr settings-subsystem ordering issue: the Memfault log backend
 * fires before settings is initialized, so the SDK boot hook cannot call
 * settings_load_subtree().  Restore is done in on_connect() instead.
 */
#define MEMFAULT_LOG_RESTORE_STATE 0
