if(CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED)
target_sources(app PRIVATE wifi_prov_over_ble.c)
endif()
