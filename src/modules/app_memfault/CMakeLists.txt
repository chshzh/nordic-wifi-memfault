# Memfault module group: core, metrics, ota, cdr
if(CONFIG_MEMFAULT)
target_sources(app PRIVATE
	core/memfault_core.c
	metrics/wifi_metrics.c
	ota/ota_triggers.c
)
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
