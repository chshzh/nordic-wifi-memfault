/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Compatibility shim for projects migrated from Partition Manager to DTS.
 *
 * NCS 3.3 deprecates Partition Manager, but some nRF libraries still include
 * <pm_config.h>. This header exposes a minimal PM-style macro set derived from
 * fixed-partition devicetree nodes so those libraries continue to build.
 */

#ifndef APP_PM_CONFIG_COMPAT_H_
#define APP_PM_CONFIG_COMPAT_H_

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>

#if DT_NODE_EXISTS(DT_NODELABEL(boot_partition))
#define PM_MCUBOOT_ADDRESS FIXED_PARTITION_OFFSET(boot_partition)
#define PM_MCUBOOT_SIZE FIXED_PARTITION_SIZE(boot_partition)
#define PM_MCUBOOT_ID FIXED_PARTITION_ID(boot_partition)
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(slot0_partition))
#define PM_MCUBOOT_PRIMARY_ADDRESS FIXED_PARTITION_OFFSET(slot0_partition)
#define PM_MCUBOOT_PRIMARY_SIZE FIXED_PARTITION_SIZE(slot0_partition)
#define PM_MCUBOOT_PRIMARY_ID FIXED_PARTITION_ID(slot0_partition)
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(slot1_partition))
#define PM_MCUBOOT_SECONDARY_ADDRESS FIXED_PARTITION_OFFSET(slot1_partition)
#define PM_MCUBOOT_SECONDARY_SIZE FIXED_PARTITION_SIZE(slot1_partition)
#define PM_MCUBOOT_SECONDARY_ID FIXED_PARTITION_ID(slot1_partition)
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
#define PM_SETTINGS_STORAGE_ADDRESS FIXED_PARTITION_OFFSET(storage_partition)
#define PM_SETTINGS_STORAGE_SIZE FIXED_PARTITION_SIZE(storage_partition)
#define PM_SETTINGS_STORAGE_ID FIXED_PARTITION_ID(storage_partition)
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(memfault_storage))
#define MEMFAULT_STORAGE memfault_storage
#define PM_MEMFAULT_STORAGE_ADDRESS FIXED_PARTITION_OFFSET(memfault_storage)
#define PM_MEMFAULT_STORAGE_SIZE FIXED_PARTITION_SIZE(memfault_storage)
#define PM_MEMFAULT_STORAGE_ID FIXED_PARTITION_ID(memfault_storage)
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(memfault_coredump_partition))
#define MEMFAULT_COREDUMP_PARTITION memfault_coredump_partition
#define PM_MEMFAULT_COREDUMP_PARTITION_ADDRESS \
	FIXED_PARTITION_OFFSET(memfault_coredump_partition)
#define PM_MEMFAULT_COREDUMP_PARTITION_SIZE \
	FIXED_PARTITION_SIZE(memfault_coredump_partition)
#define PM_MEMFAULT_COREDUMP_PARTITION_ID \
	FIXED_PARTITION_ID(memfault_coredump_partition)
#endif

#endif /* APP_PM_CONFIG_COMPAT_H_ */
