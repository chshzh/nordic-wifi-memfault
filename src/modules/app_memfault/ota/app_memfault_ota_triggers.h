/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_MEMFAULT_OTA_TRIGGERS_H
#define APP_MEMFAULT_OTA_TRIGGERS_H

#if defined(CONFIG_ZEGO_BUTTON) || defined(CONFIG_DK_LIBRARY)
void mflt_ota_triggers_notify_button(void);
#endif
void mflt_ota_triggers_notify_connected(void);

#endif /* APP_MEMFAULT_OTA_TRIGGERS_H */
