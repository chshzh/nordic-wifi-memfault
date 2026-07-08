/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * ux.c — strong overrides of zego/bricks/ux's __weak Button 0 gesture hooks.
 *
 * zego/bricks/ux's btn_listener_cb() subscribes to BUTTON_CHAN independently
 * of this app's own button listeners (app_memfault_core.c, cdr module,
 * ota_triggers module) and reacts to button_number == CONFIG_ZEGO_UX_BUTTON_IDX
 * (default 0 — the same physical button this app uses for the heartbeat/CDR
 * short-click and stack-overflow long-press demos documented in README.md).
 * Both sets of listeners run at the same zbus observer priority, so without
 * an override here:
 *   - single-click also logged "Wi-Fi mode: ..." (harmless, but undocumented
 *     noise on top of the heartbeat/CDR trigger)
 *   - long-press also raced zego/ux's default action (Wi-Fi mode cycle +
 *     sys_reboot()) against this app's intentional stack-overflow crash,
 *     with no guaranteed ordering between the two BUTTON_CHAN observers
 *
 * Overriding these two makes Button 0's behavior match README.md exactly.
 * Double-click is intentionally left at the zego/ux default (BLE-prov
 * advertising toggle / P2P pairing) — this app does not use double-click
 * for anything of its own, so there is no conflict to resolve there.
 */

#include <ux.h>

/* Single-click is fully handled by app_memfault_core.c's memfault_button_listener()
 * (heartbeat + upload trigger) and app_memfault_nrf70_fw_stats_cdr.c's
 * cdr_button_listener() (nRF70 FW stats collection) — both independent
 * BUTTON_CHAN observers. This override just suppresses zego/ux's default
 * "Wi-Fi mode" log so Button 0's single-click behavior matches README.md. */
void zego_ux_on_single_click(void)
{
}

/* Long-press is fully handled by app_memfault_core.c's memfault_button_listener()
 * (stack-overflow demo crash). This override suppresses zego/ux's default
 * Wi-Fi mode cycle + reboot so the two actions no longer race on the same
 * BUTTON_CHAN event. */
void zego_ux_on_long_press(void)
{
}
