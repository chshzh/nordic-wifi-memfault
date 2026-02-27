#
# Copyright (c) 2021 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(memfault-nrf7002dk)

# Generate hex files from pem files for HTTPS client
if(CONFIG_HTTPS_CLIENT_ENABLED)
    set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
    zephyr_include_directories(${gen_dir})
    generate_inc_file_for_target(
        app
        cert/Comodo-AAA-Certificate-Services.pem
        ${gen_dir}/Comodo-AAA-Certificate-Services.pem.inc
    )
endif()

# Set MQTT certificate folder for mqtt_helper auto-provisioning
if(CONFIG_MQTT_CLIENT_ENABLED AND CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES)
    set(MQTT_HELPER_CERTIFICATES_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/cert")
endif()

# NORDIC SDK APP START
add_subdirectory(src)
# NORDIC SDK APP END

zephyr_include_directories(src)
zephyr_include_directories(config)

# Add BLE provisioning library when enabled
if(CONFIG_BLE_PROV_ENABLED)
    zephyr_library_include_directories($<TARGET_PROPERTY:wifi_prov_core,BINARY_DIR>)
    target_link_libraries(app PRIVATE wifi_prov_ble)
endif()
