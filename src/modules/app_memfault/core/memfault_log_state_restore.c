/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Persist the Memfault ring-buffer state to a dedicated external-flash
 * partition on disconnect, and restore it into the live ring buffer at the
 * next WiFi connect so the disconnect-time log snapshot can be uploaded to
 * the Memfault cloud.
 *
 * Storage: the "mflt-log-state" fixed partition on the external SPI/QSPI NOR
 * flash (mx25r64, 8 KB).  Using a dedicated external-flash partition avoids
 * competing with the ZMS settings partition and removes the prior 3 KB cap.
 *
 * Layout written at offset 0 after a full-partition erase:
 *   [log_state_hdr (16 B)] [context bytes] [storage bytes]
 *
 * The partition is erased after a successful restore (one-shot read).
 *
 * Design note: MEMFAULT_LOG_RESTORE_STATE=1 is required to compile
 * memfault_log_get_state().  The SDK calls memfault_log_restore_state() from
 * the Zephyr log backend init (PRE_KERNEL level) before the external flash
 * driver is ready.  That stub intentionally returns false (no-op).  The
 * actual restore is performed in on_connect() instead, where the flash driver
 * is guaranteed initialized.
 */

#include "memfault_log_state_restore.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/storage/flash_map.h>

#include <memfault/core/log.h>

/*
 * Private SDK function — resets the log data-source triggered state to false.
 * Declared here (rather than via the private header) to avoid pulling internal
 * SDK headers into application code.  The function is intentionally stable
 * across SDK versions; its signature is validated by the linker.
 *
 * Why we need this:
 * If the 60-second periodic Memfault upload fires just before a disconnect and
 * the network goes down before the upload completes (e.g. DNS ENETUNREACH),
 * s_memfault_log_data_source_ctx.triggered is left as true.  This flag is NOT
 * part of the ring-buffer storage saved to/restored from flash — it lives in a
 * separate static variable in memfault_log_data_source.c.  After the ring
 * buffer is restored from flash (filling it to 4096 B), the LOG RESTORE marker
 * must be written by expiring the oldest entry.  prv_try_free_space() blocks
 * all expiry when triggered=true, so the LOG_INF() call succeeds on UART (via
 * the Zephyr UART backend) but is silently dropped from the Memfault ring
 * buffer (via the Memfault backend).  Calling memfault_log_data_source_reset()
 * clears triggered so prv_try_free_space() can proceed.  The trigger is then
 * re-established correctly by memfault_log_trigger_collection() in on_connect().
 */
extern void memfault_log_data_source_reset(void);

LOG_MODULE_REGISTER(memfault_log_state_restore, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

#define LOG_STATE_MAGIC 0x4d4c5352u /* 'MLSR' */
#define LOG_STATE_VER   2u          /* v2: external-flash backed */
#define LOG_STATE_FA_ID FIXED_PARTITION_ID(mflt_log_state_partition)

struct log_state_hdr {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t context_len;
	uint32_t storage_len;
};

BUILD_ASSERT(sizeof(struct log_state_hdr) == 16, "log_state_hdr size changed");

int memfault_log_state_persist_now(void)
{
	const struct flash_area *fa;
	sMfltLogSaveState state;
	struct log_state_hdr hdr;
	size_t total_len;
	int err;

	/* Log BEFORE capturing ring buffer state so this message is included
	 * in the persisted blob and visible in the Memfault platform.
	 */
	LOG_INF("Persisting Memfault disconnect log state to external flash...");

	state = memfault_log_get_state();
	if ((state.context == NULL) || (state.storage == NULL)) {
		return -EINVAL;
	}

	total_len = sizeof(hdr) + state.context_len + state.storage_len;

	err = flash_area_open(LOG_STATE_FA_ID, &fa);
	if (err) {
		LOG_ERR("Log-state flash open failed: %d", err);
		return err;
	}

	if (total_len > fa->fa_size) {
		LOG_WRN("Log-state too large for flash partition (%zu > %zu B)", total_len,
			(size_t)fa->fa_size);
		flash_area_close(fa);
		return -ENOSPC;
	}

	err = flash_area_erase(fa, 0, fa->fa_size);
	if (err) {
		LOG_ERR("Log-state flash erase failed: %d", err);
		flash_area_close(fa);
		return err;
	}

	/* Write payload first, header last — standard NOR atomic-commit.
	 * If a payload write fails, the partition has no valid header and
	 * restore will safely return -ENOENT instead of restoring 0xFF garbage.
	 */
	err = flash_area_write(fa, sizeof(hdr), state.context, state.context_len);
	if (err == 0) {
		err = flash_area_write(fa, sizeof(hdr) + state.context_len, state.storage,
				       state.storage_len);
	}
	if (err != 0) {
		flash_area_close(fa);
		LOG_ERR("Log-state flash write failed: %d", err);
		return err;
	}

	hdr.magic = LOG_STATE_MAGIC;
	hdr.version = LOG_STATE_VER;
	hdr.reserved = 0u;
	hdr.context_len = (uint32_t)state.context_len;
	hdr.storage_len = (uint32_t)state.storage_len;

	err = flash_area_write(fa, 0, &hdr, sizeof(hdr));
	flash_area_close(fa);

	if (err) {
		LOG_ERR("Log-state flash header write failed: %d", err);
		return err;
	}

	LOG_INF("Disconnect log-state persisted to external flash (%u + %u B)",
		(unsigned int)state.context_len, (unsigned int)state.storage_len);
	return 0;
}

int memfault_log_state_restore_on_connect(void)
{
	const struct flash_area *fa;
	struct log_state_hdr hdr;
	sMfltLogSaveState live;
	int err;

	err = flash_area_open(LOG_STATE_FA_ID, &fa);
	if (err) {
		return -ENOENT;
	}

	err = flash_area_read(fa, 0, &hdr, sizeof(hdr));
	if (err) {
		flash_area_close(fa);
		return -ENOENT;
	}

	if ((hdr.magic != LOG_STATE_MAGIC) || (hdr.version != LOG_STATE_VER) ||
	    (hdr.context_len == 0u) || (hdr.storage_len == 0u)) {
		/* Partition empty or from a different firmware — nothing to restore */
		flash_area_close(fa);
		return -ENOENT;
	}

	/* Get live pointers into the Memfault ring buffer */
	live = memfault_log_get_state();
	if ((live.context == NULL) || (live.storage == NULL)) {
		(void)flash_area_erase(fa, 0, fa->fa_size);
		flash_area_close(fa);
		return -EINVAL;
	}

	/* Size mismatch means firmware changed between disconnect and this connect.
	 * Discard the stale blob — restoring mismatched state could corrupt the
	 * ring buffer and is not safe.
	 */
	if ((hdr.context_len != (uint32_t)live.context_len) ||
	    (hdr.storage_len != (uint32_t)live.storage_len)) {
		LOG_WRN("Log-state size mismatch (firmware updated?), discarding blob");
		(void)flash_area_erase(fa, 0, fa->fa_size);
		flash_area_close(fa);
		return -ENOSPC;
	}

	/* Overwrite the live ring buffer in-place with the disconnect-time state */
	err = flash_area_read(fa, sizeof(hdr), live.context, hdr.context_len);
	if (err == 0) {
		err = flash_area_read(fa, sizeof(hdr) + hdr.context_len, live.storage,
				      hdr.storage_len);
	}

	/* One-shot: erase the partition whether restore succeeded or not */
	(void)flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	if (err) {
		LOG_ERR("Log-state flash read failed: %d", err);
		return err;
	}

	LOG_INF("Disconnect log-state restored from external flash (%u+%u B)", hdr.context_len,
		hdr.storage_len);

	/* Clear any stale "triggered" state left by a failed upload that may have
	 * occurred just before the disconnect.  prv_try_free_space() in the SDK
	 * blocks ring-buffer writes (even expiry of already-sent entries) when
	 * triggered=true, which would silently drop the LOG RESTORE marker below
	 * from the Memfault ring buffer while still printing it to UART.
	 * memfault_log_trigger_collection() in on_connect() re-establishes the
	 * trigger with the correct entry count immediately after this returns.
	 */
	memfault_log_data_source_reset();

	/* Visual separator: everything above is the pre-disconnect snapshot;
	 * everything below is the current session.
	 *
	 * Use LOG_INF + log_flush() so the marker is written synchronously into
	 * the Memfault ring buffer before this function returns.
	 *
	 * Avoid memfault_log_save_preformatted() here: prv_serialize_msg_callback
	 * in the Memfault SDK is designed to be called once per log entry but
	 * memfault_circular_buffer_read_with_callback invokes it once per
	 * contiguous region.  When the message spans the ring-buffer wrap
	 * boundary the second invocation misinterprets the wrapped bytes as a
	 * new timestamp header, causing the CBOR encoder to start a second
	 * (wrong-length) string.  The cloud decoder then only sees the first
	 * contiguous chunk — 29 chars in the observed failure.
	 * log_flush() feeds the message through the Zephyr backend in small
	 * internal pieces that never hit the boundary condition.
	 */
	LOG_INF("=== [LOG RESTORE] pre-disconnect logs above | live session below ===");
	log_flush();
	return 0;
}

/* Called by the SDK from the Zephyr log backend init (PRE_KERNEL level) before
 * the external flash driver is ready.  Always return false here; connect-time
 * restore is handled by memfault_log_state_restore_on_connect().
 */
bool memfault_log_restore_state(sMfltLogSaveState *state)
{
	(void)state;
	return false;
}
