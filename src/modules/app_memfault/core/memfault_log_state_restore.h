/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MEMFAULT_LOG_STATE_RESTORE_H
#define MEMFAULT_LOG_STATE_RESTORE_H

/**
 * Drain unread entries from the live Memfault log ring buffer and persist
 * them to the "mflt_log_state_partition" external-flash partition.
 * Call after a WiFi/network disconnect (from the 10 s debounced work item).
 *
 * Returns 0 on success, negative errno on failure.
 */
int memfault_log_state_persist_now(void);

/**
 * Load a previously persisted log blob (if any) and replay each entry back
 * into the live Memfault log ring buffer via memfault_log_save_preformatted(),
 * then call memfault_log_trigger_collection() so the replayed entries are
 * marked for upload. Call at the start of on_connect(), before
 * memfault_zephyr_port_post_data().
 *
 * On success (returns 0) a blob was found, replayed, and erased from flash.
 * Returns -ENOENT if no blob exists (normal on first boot or after upload).
 */
int memfault_log_state_restore_on_connect(void);

#endif /* MEMFAULT_LOG_STATE_RESTORE_H */
