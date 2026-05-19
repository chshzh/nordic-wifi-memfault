# Memfault module group: core, metrics, ota, cdr
if(CONFIG_MEMFAULT)
target_sources(app PRIVATE
	core/memfault_core.c
	metrics/wifi_metrics.c
	ota/ota_triggers.c
)

if(CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE)
	target_sources(app PRIVATE core/memfault_log_state_restore.c)
endif()
endif()

# nRF7002DK: DTS-partition flash coredump backend (bypasses PM dependency).
# Upstream Kconfig requires PARTITION_MANAGER_ENABLED even though the
# implementation only uses FIXED_PARTITION_* DTS macros.
if(CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM AND CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP)
	target_sources(app PRIVATE core/memfault_flash_coredump_storage.c)
endif()

# NTP module is only enabled on nrf54lm20dk; provide a CLOCK_REALTIME-backed
# memfault_platform_time_get_current() so Memfault events get wall-clock timestamps.
if(CONFIG_NTP_MODULE)
	target_sources(app PRIVATE core/memfault_platform_time.c)
endif()

if(CONFIG_MEMFAULT_NCS_STACK_METRICS)
	target_sources(app PRIVATE metrics/stack_metrics.c)
	target_include_directories(app PRIVATE ${ZEPHYR_NRF_MODULE_DIR}/modules/memfault-firmware-sdk/include)
endif()

if(CONFIG_NRF70_FW_STATS_CDR_ENABLED)
	target_sources(app PRIVATE cdr/nrf70_fw_stats_cdr.c)
	set(NRF_WIFI_DIR ${ZEPHYR_BASE}/../modules/lib/nrf_wifi)
	target_include_directories(app PRIVATE
		${ZEPHYR_BASE}/drivers/wifi/nrf_wifi/inc
		${ZEPHYR_BASE}/drivers/wifi/nrf_wifi/src
		${NRF_WIFI_DIR}/fw_if/umac_if/inc
		${NRF_WIFI_DIR}/fw_if/umac_if/inc/fw
		${NRF_WIFI_DIR}/fw_if/umac_if/inc/fw/stats
		${NRF_WIFI_DIR}/fw_if/umac_if/inc/fw/stats/system
		${NRF_WIFI_DIR}/hw_if/hal/inc
		${NRF_WIFI_DIR}/hw_if/hal/inc/common
		${NRF_WIFI_DIR}/os_if/inc
		${NRF_WIFI_DIR}/utils/inc
		${NRF_WIFI_DIR}/bus_if/bal/inc
	)
endif()
