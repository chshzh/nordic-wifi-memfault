/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * nRF70 Firmware Statistics CDR (Custom Data Recording) for Memfault.
 * Uses direct FMAC API for ON-DEMAND stats collection.
 */

#include "app_memfault_nrf70_fw_stats_cdr.h"
#include "../../messages.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/init.h>

#include "host_rpu_umac_if.h"
#include "system/fmac_api.h"
#include "fmac_main.h"
#include "rpu_lmac_phy_stats.h"
#include "rpu_umac_stats.h"
#include "memfault/components.h"
#include "memfault/core/data_packetizer.h"

#if CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE
#include <zephyr/storage/flash_map.h>
#endif

extern struct nrf_wifi_drv_priv_zep rpu_drv_priv_zep;

LOG_MODULE_REGISTER(nrf70_fw_stats_cdr, CONFIG_APP_MEMFAULT_MODULE_LOG_LEVEL);

#define NRF70_FW_STATS_BLOB_MAX_SIZE 1024

static bool has_cdr_cb(sMemfaultCdrMetadata *metadata);
static bool read_data_cb(uint32_t offset, void *buf, size_t buf_len);
static void mark_cdr_read_cb(void);
static int collect_nrf70_fw_stats(void);

static const char *const mimetypes[] = {MEMFAULT_CDR_BINARY};

static uint8_t s_nrf70_fw_stats_blob[NRF70_FW_STATS_BLOB_MAX_SIZE];
static size_t s_nrf70_fw_stats_blob_size = 0;
static bool s_cdr_data_ready = false;
static size_t s_read_offset = 0;

static sMemfaultCdrMetadata s_nrf70_fw_stats_metadata = {
	.start_time.type = kMemfaultCurrentTimeType_Unknown,
	.mimetypes = (const char **)mimetypes,
	.num_mimetypes = 1,
	.data_size_bytes = 0,
	.duration_ms = 0,
	.collection_reason = "nrf70_fw_stats",
};

static const sMemfaultCdrSourceImpl s_nrf70_fw_stats_cdr_source = {
	.has_cdr_cb = has_cdr_cb,
	.read_data_cb = read_data_cb,
	.mark_cdr_read_cb = mark_cdr_read_cb,
};

static bool has_cdr_cb(sMemfaultCdrMetadata *metadata)
{
	if (!s_cdr_data_ready || s_nrf70_fw_stats_blob_size == 0) {
		return false;
	}
	s_nrf70_fw_stats_metadata.data_size_bytes = s_nrf70_fw_stats_blob_size;
	*metadata = s_nrf70_fw_stats_metadata;
	return true;
}

static bool read_data_cb(uint32_t offset, void *buf, size_t buf_len)
{
	if (offset != s_read_offset) {
		s_read_offset = offset;
	}
	if (offset >= s_nrf70_fw_stats_blob_size) {
		return false;
	}
	size_t remaining = s_nrf70_fw_stats_blob_size - offset;
	size_t copy_len = (buf_len < remaining) ? buf_len : remaining;
	memcpy(buf, &s_nrf70_fw_stats_blob[offset], copy_len);
	s_read_offset += copy_len;
	return true;
}

static void mark_cdr_read_cb(void)
{
	s_cdr_data_ready = false;
	s_nrf70_fw_stats_blob_size = 0;
	s_read_offset = 0;
	s_nrf70_fw_stats_metadata.data_size_bytes = 0;
}

static int collect_nrf70_fw_stats(void)
{
	struct nrf_wifi_ctx_zep *ctx = &rpu_drv_priv_zep.rpu_ctx_zep;
	struct nrf_wifi_fmac_dev_ctx *fmac_dev_ctx = NULL;
	struct rpu_sys_op_stats stats;
	enum nrf_wifi_status status;
	int ret = 0;

	k_mutex_lock(&ctx->rpu_lock, K_FOREVER);

	if (!ctx->rpu_ctx) {
		ret = -ENODEV;
		goto unlock;
	}
	fmac_dev_ctx = ctx->rpu_ctx;
	memset(&stats, 0, sizeof(struct rpu_sys_op_stats));
	status = nrf_wifi_sys_fmac_stats_get(fmac_dev_ctx, 0, &stats);

	if (status != NRF_WIFI_STATUS_SUCCESS) {
		ret = -EIO;
		goto unlock;
	}

	size_t fw_stats_size = sizeof(stats.fw);
	if (fw_stats_size > NRF70_FW_STATS_BLOB_MAX_SIZE) {
		fw_stats_size = NRF70_FW_STATS_BLOB_MAX_SIZE;
	}
	memcpy(s_nrf70_fw_stats_blob, &stats.fw, fw_stats_size);
	s_nrf70_fw_stats_blob_size = fw_stats_size;

unlock:
	k_mutex_unlock(&ctx->rpu_lock);
	return ret;
}

int mflt_nrf70_fw_stats_cdr_init(void)
{
	static bool initialized = false;

	if (initialized) {
		return -EALREADY;
	}
	if (!memfault_cdr_register_source(&s_nrf70_fw_stats_cdr_source)) {
		return -EIO;
	}
	initialized = true;
	LOG_INF("nRF70 FW stats CDR module initialized");
	return 0;
}

static int nrf70_cdr_module_init(void)
{
	return mflt_nrf70_fw_stats_cdr_init();
}
SYS_INIT(nrf70_cdr_module_init, APPLICATION, 2);

int mflt_nrf70_fw_stats_cdr_collect(void)
{
	int err;

	if (s_cdr_data_ready) {
		LOG_WRN("Previous CDR data not yet uploaded, overwriting");
	}
	s_cdr_data_ready = false;
	s_nrf70_fw_stats_blob_size = 0;
	s_read_offset = 0;

	err = collect_nrf70_fw_stats();
	if (err) {
		return err;
	}
	if (s_nrf70_fw_stats_blob_size == 0) {
		return -ENODATA;
	}
	s_cdr_data_ready = true;
	LOG_INF("nRF70 FW stats CDR ready for upload (%zu bytes)", s_nrf70_fw_stats_blob_size);
	return 0;
}

size_t mflt_nrf70_fw_stats_cdr_get_size(void)
{
	return s_nrf70_fw_stats_blob_size;
}

#if CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE

#define CDR_STATE_MAGIC 0x43445253u /* 'CDRS' */
#define CDR_STATE_VER   1u
#define CDR_STATE_FA_ID FIXED_PARTITION_ID(mflt_cdr_state_partition)

struct cdr_state_hdr {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t blob_size;
	uint32_t reserved2;
};

BUILD_ASSERT(sizeof(struct cdr_state_hdr) == 16, "cdr_state_hdr size changed");

int mflt_nrf70_fw_stats_cdr_persist_to_flash(void)
{
	const struct flash_area *fa;
	struct cdr_state_hdr hdr;
	int err;

	LOG_INF("Collecting nRF70 CDR data for disconnect-time persist...");

	s_cdr_data_ready = false;
	s_nrf70_fw_stats_blob_size = 0;
	s_read_offset = 0;

	err = collect_nrf70_fw_stats();
	if (err) {
		LOG_WRN("nRF70 CDR collect failed for flash persist: %d", err);
		return err;
	}
	if (s_nrf70_fw_stats_blob_size == 0) {
		return -ENODATA;
	}
	s_cdr_data_ready = true;

	err = flash_area_open(CDR_STATE_FA_ID, &fa);
	if (err) {
		LOG_ERR("CDR state flash open failed: %d", err);
		return err;
	}

	size_t total_len = sizeof(hdr) + s_nrf70_fw_stats_blob_size;

	if (total_len > fa->fa_size) {
		LOG_WRN("CDR state too large for flash partition (%zu > %zu B)", total_len,
			(size_t)fa->fa_size);
		flash_area_close(fa);
		return -ENOSPC;
	}

	err = flash_area_erase(fa, 0, fa->fa_size);
	if (err) {
		LOG_ERR("CDR state flash erase failed: %d", err);
		flash_area_close(fa);
		return err;
	}

	/* Write payload first, header last — standard NOR atomic-commit pattern. */
	err = flash_area_write(fa, sizeof(hdr), s_nrf70_fw_stats_blob, s_nrf70_fw_stats_blob_size);
	if (err != 0) {
		flash_area_close(fa);
		LOG_ERR("CDR state flash write failed: %d", err);
		return err;
	}

	hdr.magic = CDR_STATE_MAGIC;
	hdr.version = CDR_STATE_VER;
	hdr.reserved = 0u;
	hdr.blob_size = (uint32_t)s_nrf70_fw_stats_blob_size;
	hdr.reserved2 = 0u;

	err = flash_area_write(fa, 0, &hdr, sizeof(hdr));
	flash_area_close(fa);

	if (err) {
		LOG_ERR("CDR state flash header write failed: %d", err);
		return err;
	}

	LOG_INF("Disconnect CDR state persisted to flash (%u bytes)",
		(unsigned int)s_nrf70_fw_stats_blob_size);
	return 0;
}

int mflt_nrf70_fw_stats_cdr_restore_from_flash(void)
{
	const struct flash_area *fa;
	struct cdr_state_hdr hdr;
	int err;

	err = flash_area_open(CDR_STATE_FA_ID, &fa);
	if (err) {
		return -ENOENT;
	}

	err = flash_area_read(fa, 0, &hdr, sizeof(hdr));
	if (err) {
		flash_area_close(fa);
		return -ENOENT;
	}

	if ((hdr.magic != CDR_STATE_MAGIC) || (hdr.version != CDR_STATE_VER) ||
	    (hdr.blob_size == 0u) || (hdr.blob_size > NRF70_FW_STATS_BLOB_MAX_SIZE)) {
		/* Partition empty or invalid header */
		flash_area_close(fa);
		return -ENOENT;
	}

	err = flash_area_read(fa, sizeof(hdr), s_nrf70_fw_stats_blob, hdr.blob_size);

	/* One-shot: erase partition whether restore succeeded or not */
	(void)flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	if (err) {
		LOG_ERR("CDR state flash read failed: %d", err);
		return err;
	}

	s_nrf70_fw_stats_blob_size = hdr.blob_size;
	s_nrf70_fw_stats_metadata.data_size_bytes = s_nrf70_fw_stats_blob_size;
	s_cdr_data_ready = true;
	s_read_offset = 0;

	LOG_INF("Disconnect CDR state restored from flash (%u bytes)", (unsigned int)hdr.blob_size);
	return 0;
}

#endif /* CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE */

#if CONFIG_NRF70_FW_STATS_CDR_ENABLED
/* Zbus: Button 1 short press -> collect nRF70 FW stats (memfault_core will post_data) */
extern const struct zbus_channel BUTTON_CHAN;

static void cdr_button_listener(const struct zbus_channel *chan)
{
	const struct button_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type != BUTTON_RELEASED || msg->button_number != 1) {
		return;
	}
	if (msg->duration_ms >= BUTTON_LONG_PRESS_THRESHOLD_MS) {
		return;
	}
	int err = mflt_nrf70_fw_stats_cdr_collect();
	if (err) {
		LOG_WRN("nRF70 FW stats CDR collection failed: %d", err);
	} else {
		LOG_INF("nRF70 FW stats CDR collected (%zu bytes)",
			mflt_nrf70_fw_stats_cdr_get_size());
	}
}

ZBUS_LISTENER_DEFINE(cdr_button_listener_def, cdr_button_listener);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, cdr_button_listener_def, 0);
#endif
