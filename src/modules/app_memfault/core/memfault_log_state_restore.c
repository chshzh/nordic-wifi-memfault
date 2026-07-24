/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Persist unread Memfault log entries to a dedicated external-flash
 * partition on disconnect, and replay them into the live log ring buffer at
 * the next WiFi connect so the disconnect-time log trail can be uploaded to
 * the Memfault cloud.
 *
 * Storage: the "mflt_log_state_partition" static partition on the external
 * SPI/QSPI NOR flash (MX25R64, 12 KB, see pm_static_<board>.yml).
 *
 * Design note: the Memfault Firmware SDK version bundled with NCS v2.6.4
 * (modules/lib/memfault-firmware-sdk, v1.6.0) has no raw ring-buffer
 * save/restore API (memfault_log_get_state() / memfault_log_restore_state()
 * do not exist in this SDK version). Instead of copying the raw ring-buffer
 * memory, this module drains unread entries one at a time with
 * memfault_log_read() -- exactly the intended low-priority "flush to slower
 * medium" use case documented in memfault/core/log.h -- serializes each
 * entry (level + type + message bytes) to flash, and on reconnect replays
 * each entry back in with memfault_log_save_preformatted(). Compact-log
 * entries (kMemfaultLogRecordType_Compact) are skipped: this app does not
 * enable CONFIG_MEMFAULT_COMPACT_LOG_ENABLE, so none are expected in
 * practice, and replaying arbitrary compact bytes without the original ELF
 * fmt-id table would be unsafe.
 *
 * Trade-off vs. a raw-memory restore: replayed entries carry the restore-time
 * (NTP) timestamp, not the exact original disconnect-time timestamp.
 */

#include "memfault_log_state_restore.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include <memfault/core/log.h>
#include <pm_config.h>

/* Private SDK function -- resets the log data-source "triggered" state.
 * Declared here rather than via the private header to avoid pulling internal
 * SDK headers into application code; the function is defined unconditionally
 * in memfault_log_data_source.c (not unit-test gated) on this SDK version.
 *
 * Why we need this: if the periodic Memfault upload triggers just before a
 * disconnect and the network goes down before the upload completes, the
 * internal "triggered" flag in memfault_log_data_source.c is left true. This
 * flag is not part of the ring buffer itself. Once it is true,
 * memfault_log_trigger_collection() becomes a no-op (it only (re)counts
 * unsent logs the first time it's called), so entries replayed by this
 * restore would not be included in the upload. Resetting the flag lets
 * memfault_log_trigger_collection() in on_connect() recount correctly after
 * restore.
 */
extern void memfault_log_data_source_reset(void);

LOG_MODULE_REGISTER(memfault_log_state_restore, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

#define LOG_STATE_MAGIC     0x4d4c5352u /* 'MLSR' */
#define LOG_STATE_VER       3u          /* v3: drain-and-replay entry list */
#define LOG_STATE_FA_ID     PM_MFLT_LOG_STATE_PARTITION_ID

/* The scratch buffer only ever needs to hold what memfault_log_read() can
 * drain from the live ring buffer -- bounded by CONFIG_MEMFAULT_LOGGING_RAM_SIZE
 * (8 KB), not the (larger, headroom-padded) 12 KB flash partition size.
 * RAM is tight on this target (nRF7002DK build is at ~99.6% RAM use), so
 * this is further capped to 4 KB: the drain loop below stops early and logs
 * a warning if the live ring buffer holds more unread data than that, but a
 * static 8 KB buffer is not affordable here.
 */
#define LOG_STATE_BLOB_SIZE 4096

struct log_state_hdr {
	uint32_t magic;
	uint16_t version;
	uint16_t entry_count;
	uint32_t payload_len;
	uint32_t reserved;
};

BUILD_ASSERT(sizeof(struct log_state_hdr) == 16, "log_state_hdr size changed");

struct log_entry_hdr {
	uint8_t level;
	uint8_t type;
	uint16_t msg_len;
};

/* Scratch buffer for draining/replaying entries. Only used transiently
 * around a disconnect/reconnect event, so a static (BSS) buffer is used
 * instead of consuming stack.
 */
static uint8_t s_log_state_blob[LOG_STATE_BLOB_SIZE];

int memfault_log_state_persist_now(void)
{
	const struct flash_area *fa;
	struct log_state_hdr hdr = {0};
	size_t offset = 0;
	uint16_t entry_count = 0;
	int err;

	LOG_INF("Draining Memfault log ring buffer for disconnect-time persist...");

	uint32_t total_read = 0;
	uint32_t evicted = 0;

	while (true) {
		sMemfaultLog log = {0};

		if (!memfault_log_read(&log)) {
			break;
		}

		if (log.type != kMemfaultLogRecordType_Preformatted) {
			/* Compact logs require the ELF fmt-id table to replay;
			 * not expected (compact logging disabled), skip defensively.
			 */
			continue;
		}

		total_read++;

		size_t entry_size = sizeof(struct log_entry_hdr) + log.msg_len;

		if (entry_size > sizeof(s_log_state_blob)) {
			/* Single entry larger than the whole scratch buffer -- can
			 * never fit, skip it rather than evicting everything for it.
			 */
			continue;
		}

		/* Keep the entries closest to the disconnect event: if the ring
		 * buffer holds more unread backlog than this 4 KB budget (e.g.
		 * because periodic Memfault uploads have been failing, so old
		 * entries were never marked collected), evict from the front
		 * (oldest retained entry) rather than stopping the drain -- the
		 * disconnect-time log trail is most valuable near the disconnect
		 * itself, not at whatever old entries happened to be queued first.
		 */
		while ((offset + entry_size) > sizeof(s_log_state_blob)) {
			struct log_entry_hdr front_hdr;

			memcpy(&front_hdr, &s_log_state_blob[0], sizeof(front_hdr));
			size_t front_size = sizeof(front_hdr) + front_hdr.msg_len;

			memmove(&s_log_state_blob[0], &s_log_state_blob[front_size],
				offset - front_size);
			offset -= front_size;
			entry_count--;
			evicted++;
		}

		struct log_entry_hdr ehdr = {
			.level = (uint8_t)log.level,
			.type = (uint8_t)log.type,
			.msg_len = (uint16_t)log.msg_len,
		};

		memcpy(&s_log_state_blob[offset], &ehdr, sizeof(ehdr));
		offset += sizeof(ehdr);
		memcpy(&s_log_state_blob[offset], log.msg, log.msg_len);
		offset += log.msg_len;
		entry_count++;
	}

	if (evicted > 0) {
		LOG_WRN("Log-state blob full, kept newest %u of %u entries",
			entry_count, total_read);
	}

	if (entry_count == 0) {
		return -ENODATA;
	}

	err = flash_area_open(LOG_STATE_FA_ID, &fa);
	if (err) {
		LOG_ERR("Log-state flash open failed: %d", err);
		return err;
	}

	if ((sizeof(hdr) + offset) > fa->fa_size) {
		LOG_WRN("Log-state too large for flash partition (%zu > %zu B)",
			sizeof(hdr) + offset, (size_t)fa->fa_size);
		flash_area_close(fa);
		return -ENOSPC;
	}

	err = flash_area_erase(fa, 0, fa->fa_size);
	if (err) {
		LOG_ERR("Log-state flash erase failed: %d", err);
		flash_area_close(fa);
		return err;
	}

	/* Write payload first, header last -- standard NOR atomic-commit.
	 * If a payload write fails, the partition has no valid header and
	 * restore will safely return -ENOENT instead of restoring 0xFF garbage.
	 */
	err = flash_area_write(fa, sizeof(hdr), s_log_state_blob, offset);
	if (err != 0) {
		flash_area_close(fa);
		LOG_ERR("Log-state flash write failed: %d", err);
		return err;
	}

	hdr.magic = LOG_STATE_MAGIC;
	hdr.version = LOG_STATE_VER;
	hdr.entry_count = entry_count;
	hdr.payload_len = (uint32_t)offset;

	err = flash_area_write(fa, 0, &hdr, sizeof(hdr));
	flash_area_close(fa);

	if (err) {
		LOG_ERR("Log-state flash header write failed: %d", err);
		return err;
	}

	LOG_INF("Disconnect log-state persisted to external flash (%u entries, %zu B)",
		entry_count, offset);
	return 0;
}

int memfault_log_state_restore_on_connect(void)
{
	const struct flash_area *fa;
	struct log_state_hdr hdr;
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
	    (hdr.entry_count == 0u) || (hdr.payload_len == 0u) ||
	    (hdr.payload_len > sizeof(s_log_state_blob))) {
		/* Partition empty or from a different/older firmware -- nothing
		 * to restore, or unsafe to trust.
		 */
		flash_area_close(fa);
		return -ENOENT;
	}

	err = flash_area_read(fa, sizeof(hdr), s_log_state_blob, hdr.payload_len);

	/* One-shot: erase the partition whether restore succeeded or not */
	(void)flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	if (err) {
		LOG_ERR("Log-state flash read failed: %d", err);
		return err;
	}

	size_t offset = 0;
	uint16_t replayed = 0;

	for (uint16_t i = 0; i < hdr.entry_count; i++) {
		if ((offset + sizeof(struct log_entry_hdr)) > hdr.payload_len) {
			LOG_WRN("Log-state blob truncated, stopping replay early (%u/%u entries)",
				replayed, hdr.entry_count);
			break;
		}

		struct log_entry_hdr ehdr;

		memcpy(&ehdr, &s_log_state_blob[offset], sizeof(ehdr));
		offset += sizeof(ehdr);

		if ((offset + ehdr.msg_len) > hdr.payload_len) {
			LOG_WRN("Log-state blob truncated, stopping replay early (%u/%u entries)",
				replayed, hdr.entry_count);
			break;
		}

		memfault_log_save_preformatted((eMemfaultPlatformLogLevel)ehdr.level,
					       (const char *)&s_log_state_blob[offset],
					       ehdr.msg_len);
		offset += ehdr.msg_len;
		replayed++;
	}

	LOG_INF("Disconnect log-state restored from external flash (%u entries)", replayed);

	/* Clear any stale "triggered" state left by a failed upload that may have
	 * occurred just before the disconnect -- see file header comment.
	 */
	memfault_log_data_source_reset();
	memfault_log_trigger_collection();

	/* Visual separator: everything above is the pre-disconnect snapshot;
	 * everything below is the current session.
	 */
	LOG_INF("=== [LOG RESTORE] pre-disconnect logs above | live session below ===");
	return 0;
}
