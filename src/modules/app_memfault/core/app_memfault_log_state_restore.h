/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_MEMFAULT_LOG_STATE_RESTORE_H
#define APP_MEMFAULT_LOG_STATE_RESTORE_H

/**
 * Persist the current Memfault ring-buffer state to Zephyr settings storage.
 * Call immediately after memfault_log_trigger_collection() on disconnect.
 * Returns 0 on success, negative errno on failure.
 */
int memfault_log_state_persist_now(void);

/**
 * Load a previously persisted ring-buffer state from settings and restore it
 * into the live Memfault ring buffer in-place.  Call at the start of
 * on_connect(), before memfault_zephyr_port_post_data().  Settings must be
 * initialized before this is called (guaranteed at APPLICATION level).
 *
 * On success (returns 0) the ring buffer contains the disconnect-time frozen
 * snapshot; the stored blob is deleted.  The subsequent upload will deliver
 * the snapshot to Memfault cloud.
 *
 * Returns 0 if a blob was found and restored.
 * Returns -ENOENT if no blob exists (normal on first boot or after upload).
 * Returns -ENOSPC if the persisted sizes do not match the current ring buffer
 *         (firmware was updated between the disconnect and this connect).
 */
int memfault_log_state_restore_on_connect(void);

#endif /* APP_MEMFAULT_LOG_STATE_RESTORE_H */
