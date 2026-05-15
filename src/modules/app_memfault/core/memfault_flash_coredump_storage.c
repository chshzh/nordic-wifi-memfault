/*
 * Copyright (c) 2021 Memfault Inc.
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Flash-backed Memfault coredump storage for nRF7002DK (nRF5340).
 *
 * This is a copy of nrf/modules/memfault-firmware-sdk/memfault_flash_coredump_storage.c
 * placed here to bypass the PARTITION_MANAGER_ENABLED dependency in the upstream NCS
 * Kconfig. The implementation only uses FIXED_PARTITION_* DTS macros, which work
 * without Partition Manager when a `memfault_storage` fixed partition exists in DTS.
 *
 * Upstream issue: CONFIG_MEMFAULT_NCS_INTERNAL_FLASH_BACKED_COREDUMP has
 * `depends on PARTITION_MANAGER_ENABLED` even though the C code does not require PM.
 * See: https://github.com/NordicSemiconductor/sdk-nrf/issues/<TBD>
 */

#include <zephyr/kernel.h>
#include <nrfx_nvmc.h>
#include <zephyr/storage/flash_map.h>

#include <memfault/components.h>
#include <memfault/ports/buffered_coredump_storage.h>

static bool last_coredump_cleared;

void memfault_platform_coredump_storage_get_info(sMfltCoredumpStorageInfo *info)
{
	*info = (sMfltCoredumpStorageInfo){
		.size = FIXED_PARTITION_SIZE(memfault_storage),
	};
}

static bool prv_op_within_flash_bounds(uint32_t offset, size_t data_len)
{
	sMfltCoredumpStorageInfo info = {0};

	memfault_platform_coredump_storage_get_info(&info);
	return (offset + data_len) <= info.size;
}

bool memfault_platform_coredump_storage_read(uint32_t offset, void *data, size_t read_len)
{
	if (!prv_op_within_flash_bounds(offset, read_len)) {
		return false;
	}

	const uint32_t address = FIXED_PARTITION_OFFSET(memfault_storage) + offset;

	memcpy(data, (void *)address, read_len);
	return true;
}

bool memfault_platform_coredump_storage_erase(uint32_t offset, size_t erase_size)
{
	uint32_t page_size = nrfx_nvmc_flash_page_size_get();

	if (!prv_op_within_flash_bounds(offset, erase_size)) {
		return false;
	}

	if ((offset % page_size) != 0) {
		return false;
	}

	for (size_t page = offset; page < erase_size; page += page_size) {
		const uint32_t address = FIXED_PARTITION_OFFSET(memfault_storage) + page;

		nrfx_nvmc_page_erase(address);
	}

	return true;
}

bool memfault_platform_coredump_storage_buffered_write(sCoredumpWorkingBuffer *blk)
{
	const uint32_t start_addr = FIXED_PARTITION_OFFSET(memfault_storage);
	const uint32_t addr = start_addr + blk->write_offset;

	if (!prv_op_within_flash_bounds(blk->write_offset, MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE)) {
		return false;
	}

	MEMFAULT_STATIC_ASSERT(MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE % sizeof(uint32_t) == 0,
			       "Write buffer must be word aligned");
	nrfx_nvmc_words_write(addr, &blk->data[0],
			      MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE / sizeof(uint32_t));
	return true;
}

bool memfault_coredump_read(uint32_t offset, void *data, size_t read_len)
{
	if (last_coredump_cleared) {
		memset(data, 0x0, read_len);
		return true;
	}

	return memfault_platform_coredump_storage_read(offset, data, read_len);
}

void memfault_platform_coredump_storage_clear(void)
{
	uint32_t empty_word = 0x0;
	const struct flash_area *flash_area;
	int err;

	err = flash_area_open(FIXED_PARTITION_ID(memfault_storage), &flash_area);
	if (err) {
		MEMFAULT_LOG_ERROR("Unable to open coredump storage: 0x%x", err);
		return;
	}

	err = flash_area_write(flash_area, 0x0, &empty_word, sizeof(empty_word));
	if (err) {
		MEMFAULT_LOG_ERROR("Unable to clear storage: 0x%x", err);
		return;
	}

	last_coredump_cleared = true;
}
