#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Add network event management
target_sources(app PRIVATE
                     ${CMAKE_CURRENT_SOURCE_DIR}/net_event_mgmt.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/wifi_utils.c)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(app PUBLIC ${ZEPHYR_BASE}/subsys/net/ip)
