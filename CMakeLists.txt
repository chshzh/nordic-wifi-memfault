#
# Copyright (c) 2026 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Register zego modules: button, led, wifi, network, wifi_ble_prov, memonitor.
get_filename_component(ZEGO_BUTTON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/button REALPATH)
get_filename_component(ZEGO_LED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/led REALPATH)
get_filename_component(ZEGO_WIFI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/wifi REALPATH)
get_filename_component(ZEGO_NETWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/network REALPATH)
get_filename_component(ZEGO_WIFI_BLE_PROV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/wifi_ble_prov REALPATH)
get_filename_component(ZEGO_MEMONITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/memonitor REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_BUTTON_DIR} ${ZEGO_LED_DIR} ${ZEGO_WIFI_DIR} ${ZEGO_NETWORK_DIR} ${ZEGO_WIFI_BLE_PROV_DIR} ${ZEGO_MEMONITOR_DIR})

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

# APP_VERSION_STRING, PRD_VERSION, SPECS_VERSION set by zego/wifi/CMakeLists.txt
target_sources(app PRIVATE src/main.c)

# Module subdirectories (SMF+zbus modular architecture)
# button module now provided by zego/button (EXTRA_ZEPHYR_MODULES above)
# Shared network module provided by zego/network (EXTRA_ZEPHYR_MODULES above);
# app shim (net_event_app.c) defines NETWORK_CHAN, declares WIFI_CHAN, overrides weak callbacks.
if(CONFIG_ZEGO_NETWORK)
    add_subdirectory(src/modules/network)
endif()
if(CONFIG_APP_MEMFAULT_MODULE)
    add_subdirectory(src/modules/app_memfault)
endif()
if(CONFIG_APP_HTTPS_CLIENT_MODULE)
    add_subdirectory(src/modules/app_https_client)
endif()
if(CONFIG_APP_MQTT_CLIENT_MODULE)
    add_subdirectory(src/modules/app_mqtt_client)
endif()
if(CONFIG_NTP_MODULE)
    add_subdirectory(src/modules/ntp)
endif()
if(CONFIG_APP_UX_MODULE)
    add_subdirectory(src/modules/ux)
endif()

# zego/wifi_ble_prov handles BLE provisioning as an EXTRA_ZEPHYR_MODULE (no extra target_link_libraries needed).
# zego/led is registered above and provides LED_CMD_CHAN; consumed by src/modules/ux.
