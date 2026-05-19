/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Persist the Memfault ring-buffer state to Zephyr settings on disconnect,
 * and restore it into the live ring buffer at the next WiFi connect so the
 * disconnect-time log snapshot can be uploaded to the Memfault cloud.
 *
 * Design note: the Memfault SDK's MEMFAULT_LOG_RESTORE_STATE boot hook fires
 * from the Zephyr log backend init (PRE_KERNEL / POST_KERNEL level), before
 * Zephyr settings is initialized (APPLICATION level ~95).  Calling
 * settings_load_subtree() that early silently fails and the restore is
 * skipped.  We therefore set MEMFAULT_LOG_RESTORE_STATE=0 and perform the
 * restore in on_connect() instead, where settings is guaranteed ready.
 */

#include "memfault_log_state_restore.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <memfault/core/log.h>

LOG_MODULE_REGISTER(memfault_log_state_restore, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

#define LOG_STATE_MAGIC   0x4d4c5352u /* MLSR */
#define LOG_STATE_VER     1u
#define LOG_STATE_KEY     "app_memfault/log_state/blob"
#define LOG_STATE_SUBTREE "app_memfault/log_state"

struct log_state_hdr {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t context_len;
	uint32_t storage_len;
};

static uint8_t blob_buf[sizeof(struct log_state_hdr) + MEMFAULT_LOG_STATE_SIZE_BYTES +
			CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE_MAX_BYTES];

static size_t pending_blob_len;
static bool pending_blob_valid;
static K_MUTEX_DEFINE(log_state_lock);

/* Settings load callback — copies raw blob bytes into blob_buf */
static int log_state_settings_set(const char *name, size_t len_rd, settings_read_cb read_cb,
				  void *cb_arg)
{
	int rc;

	if (strcmp(name, "blob") != 0) {
		return -ENOENT;
	}
	if ((len_rd < sizeof(struct log_state_hdr)) || (len_rd > sizeof(blob_buf))) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, blob_buf, len_rd);
	if (rc < 0) {
		return rc;
	}
	if ((size_t)rc != len_rd) {
		return -EIO;
	}

	k_mutex_lock(&log_state_lock, K_FOREVER);
	pending_blob_len = len_rd;
	pending_blob_valid = true;
	k_mutex_unlock(&log_state_lock);

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(memfault_log_state, LOG_STATE_SUBTREE, NULL, log_state_settings_set,
			       NULL, NULL);

int memfault_log_state_persist_now(void)
{
	sMfltLogSaveState state;
	struct log_state_hdr hdr = {
		.magic = LOG_STATE_MAGIC,
		.version = LOG_STATE_VER,
		.reserved = 0u,
	};
	size_t total_len;
	int err;

	state = memfault_log_get_state();
	if ((state.context == NULL) || (state.storage == NULL)) {
		return -EINVAL;
	}

	if ((state.storage_len > CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE_MAX_BYTES) ||
	    (state.context_len > MEMFAULT_LOG_STATE_SIZE_BYTES)) {
		LOG_WRN("Log-state too large for settings storage (%u + %u B)",
			(unsigned int)state.context_len, (unsigned int)state.storage_len);
		return -ENOSPC;
	}

	hdr.context_len = (uint32_t)state.context_len;
	hdr.storage_len = (uint32_t)state.storage_len;
	total_len = sizeof(hdr) + state.context_len + state.storage_len;

	if (total_len > sizeof(blob_buf)) {
		return -ENOSPC;
	}

	memcpy(blob_buf, &hdr, sizeof(hdr));
	memcpy(&blob_buf[sizeof(hdr)], state.context, state.context_len);
	memcpy(&blob_buf[sizeof(hdr) + state.context_len], state.storage, state.storage_len);

	err = settings_save_one(LOG_STATE_KEY, blob_buf, total_len);
	if (err) {
		LOG_ERR("Log-state settings save failed: %d", err);
		return err;
	}

	LOG_INF("Disconnect log-state persisted (%u + %u B)", (unsigned int)state.context_len,
		(unsigned int)state.storage_len);
	return 0;
}

int memfault_log_state_restore_on_connect(void)
{
	struct log_state_hdr hdr;
	sMfltLogSaveState live;
	int err;

	err = settings_load_subtree(LOG_STATE_SUBTREE);
	if (err) {
		return -ENOENT;
	}

	k_mutex_lock(&log_state_lock, K_FOREVER);
	if (!pending_blob_valid || (pending_blob_len < sizeof(hdr))) {
		k_mutex_unlock(&log_state_lock);
		return -ENOENT;
	}

	memcpy(&hdr, blob_buf, sizeof(hdr));

	if ((hdr.magic != LOG_STATE_MAGIC) || (hdr.version != LOG_STATE_VER) ||
	    (hdr.context_len == 0u) || (hdr.storage_len == 0u)) {
		pending_blob_valid = false;
		k_mutex_unlock(&log_state_lock);
		settings_delete(LOG_STATE_KEY);
		return -EINVAL;
	}

	/* Get live pointers into the Memfault ring buffer */
	live = memfault_log_get_state();

	if ((live.context == NULL) || (live.storage == NULL)) {
		pending_blob_valid = false;
		k_mutex_unlock(&log_state_lock);
		settings_delete(LOG_STATE_KEY);
		return -EINVAL;
	}

	/* Size mismatch means firmware changed between disconnect and this connect.
	 * Discard the stale blob — uploading mismatched state could corrupt the
	 * ring buffer and is not safe.
	 */
	if ((hdr.context_len != (uint32_t)live.context_len) ||
	    (hdr.storage_len != (uint32_t)live.storage_len)) {
		LOG_WRN("Log-state size mismatch (firmware updated?), discarding blob");
		pending_blob_valid = false;
		k_mutex_unlock(&log_state_lock);
		settings_delete(LOG_STATE_KEY);
		return -ENOSPC;
	}

	/* Overwrite the live ring buffer in-place with the disconnect-time state.
	 * live.context points to s_memfault_ram_logger (static, same address for
	 * same-firmware builds).  live.storage points to s_mflt_log_buf_storage
	 * (also static).  The size check above ensures layout compatibility.
	 */
	memcpy(live.context, &blob_buf[sizeof(hdr)], hdr.context_len);
	memcpy(live.storage, &blob_buf[sizeof(hdr) + hdr.context_len], hdr.storage_len);

	pending_blob_valid = false;
	pending_blob_len = 0u;
	k_mutex_unlock(&log_state_lock);

	err = settings_delete(LOG_STATE_KEY);
	if (err) {
		LOG_WRN("Log-state settings clear failed: %d", err);
	}

	LOG_INF("Disconnect log-state restored into live ring buffer");
	return 0;
}
