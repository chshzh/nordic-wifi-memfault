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
 * Collect a fresh nRF70 CDR snapshot and persist it to the
 * "mflt_cdr_state_partition" external-flash partition. Call from the 10 s
 * debounced disconnect work item, after memfault_log_state_persist_now().
 * Returns 0 on success, negative errno on failure.
 */
int mflt_nrf70_fw_stats_cdr_persist_to_flash(void);

/**
 * Load a previously persisted CDR blob (if any) from flash into the CDR
 * source buffer so the existing has_cdr_cb() upload path picks it up on the
 * next memfault_zephyr_port_post_data() call. Call at the start of
 * on_connect().
 *
 * Returns 0 if a blob was found and restored.
 * Returns -ENOENT if no blob exists (normal on first boot or after upload).
 */
int mflt_nrf70_fw_stats_cdr_restore_from_flash(void);
#endif /* CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE */

#endif /* NRF70_FW_STATS_CDR_H */
