#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_NTP_MODULE_ENABLED)
target_sources(app PRIVATE ntp.c)
endif()
