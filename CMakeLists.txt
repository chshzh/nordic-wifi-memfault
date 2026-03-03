#
# Copyright (c) 2026 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(memfault-nrf7002dk)

# Generate hex files from pem files for app HTTPS client
if(CONFIG_APP_HTTPS_CLIENT_MODULE)
    set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
    zephyr_include_directories(${gen_dir})
    generate_inc_file_for_target(
        app
        src/modules/app_https_client/cert/Comodo-AAA-Certificate-Services.pem
        ${gen_dir}/Comodo-AAA-Certificate-Services.pem.inc
    )
endif()


zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/modules/app_memfault/config)

target_sources(app PRIVATE src/main.c)

# Module subdirectories (SMF+zbus modular architecture)
add_subdirectory(src/modules/button)
add_subdirectory(src/modules/network)
add_subdirectory(src/modules/wifi_prov_over_ble)
if(CONFIG_APP_MEMFAULT_MODULE)
    add_subdirectory(src/modules/app_memfault)
endif()
add_subdirectory(src/modules/app_https_client)
add_subdirectory(src/modules/app_mqtt_client)

# Add BLE provisioning library when enabled
if(CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED)
    zephyr_library_include_directories($<TARGET_PROPERTY:wifi_prov_core,BINARY_DIR>)
    target_link_libraries(app PRIVATE wifi_prov_ble)
endif()
