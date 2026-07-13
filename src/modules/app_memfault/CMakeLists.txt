# Memfault module group: core, metrics, ota, cdr
if(CONFIG_MEMFAULT)
target_sources(app PRIVATE
	core/memfault_core.c
	metrics/wifi_metrics.c
	ota/ota_triggers.c
)
endif()

if(CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE)
	target_sources(app PRIVATE core/memfault_log_state_restore.c)
endif()

if(CONFIG_MEMFAULT_NCS_STACK_METRICS)
	target_sources(app PRIVATE metrics/stack_metrics.c)
	target_include_directories(app PRIVATE ${ZEPHYR_NRF_MODULE_DIR}/modules/memfault-firmware-sdk/include)
endif()

if(CONFIG_NRF70_FW_STATS_CDR_ENABLED)
	target_sources(app PRIVATE cdr/nrf70_fw_stats_cdr.c)
	# On NCS v2.6.4 the nrf700x driver (nrf/drivers/wifi/nrf700x/CMakeLists.txt)
	# already exposes its internal headers (fmac_api.h, host_rpu_umac_if.h,
	# fmac_main.h, etc.) via a global zephyr_include_directories() call, so no
	# extra include dirs are needed here.
endif()
