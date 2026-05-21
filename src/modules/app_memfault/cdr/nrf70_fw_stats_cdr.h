/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NRF70_FW_STATS_CDR_H
#define NRF70_FW_STATS_CDR_H

#include <stddef.h>

int mflt_nrf70_fw_stats_cdr_init(void);
int mflt_nrf70_fw_stats_cdr_collect(void);
size_t mflt_nrf70_fw_stats_cdr_get_size(void);

#if CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE
/**
 * Collect fresh nRF70 fw stats and persist the blob to the external flash
 * "mflt-cdr-state" partition (8 KB).  Call from log_freeze_work_fn() after
 * the log-state persist.  Returns 0 on success, negative errno on failure.
 */
int mflt_nrf70_fw_stats_cdr_persist_to_flash(void);

/**
 * Load a previously persisted CDR blob from the "mflt-cdr-state" external
 * flash partition into the CDR source buffer and mark data as ready for
 * upload.  Call from on_connect() before memfault_zephyr_port_post_data().
 * The partition is erased after a successful restore.
 *
 * Returns 0 if a blob was found and restored.
 * Returns -ENOENT if no valid blob exists.
 */
int mflt_nrf70_fw_stats_cdr_restore_from_flash(void);
#endif /* CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE */

#endif /* NRF70_FW_STATS_CDR_H */
