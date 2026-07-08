# UX Module Engineering Spec — LED Wi-Fi Feedback

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-memfault |
| Version | 2026-07-08-00-00 |
| PRD Version | 2026-07-07-16-32 |
| NCS Version | v3.4.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Implemented — gesture-hook overrides only (LED feedback remains owned by `zego/bricks/ux`) |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-08-00-00 | **Module re-introduced** (gesture-hook overrides only — not LED duplication). `src/modules/ux/ux.c` (`CONFIG_ZEGO_UX`, added unconditionally via `CMakeLists.txt`) provides strong overrides for `zego/bricks/ux`'s `__weak zego_ux_on_single_click()` and `zego_ux_on_long_press()`. Root cause: `zego/bricks/ux`'s own `BUTTON_CHAN` listener reacts to `button_number == CONFIG_ZEGO_UX_BUTTON_IDX` (default `0`), the same physical button this app uses for the heartbeat/CDR short-click and stack-overflow long-press demos (README.md Buttons table). Both listener sets ran at the same zbus observer priority with no defined ordering: single-click added an undocumented extra "Wi-Fi mode" log line, and long-press raced the app's intentional stack-overflow crash against zego/ux's default Wi-Fi-mode-cycle-and-reboot action — the crash demo was not reliably reproducible. The two overrides are no-ops; double-click is intentionally left at the zego/ux default (BLE-prov advertising toggle / P2P pairing) since this app assigns no behavior of its own to it. |
| 2026-07-07-16-32 | PRD Version updated to 2026-07-07-16-32. The local `src/modules/ux/` (`CONFIG_APP_UX_MODULE`) duplicated LED 0 Wi-Fi-state feedback now provided by `zego/bricks/ux` (`CONFIG_ZEGO_UX`), which this project already links in for the startup banner. Both modules were independently driving LED 0 (confirmed on hardware: duplicate "LED ROTATE effect started" at boot from two separate `SYS_INIT` handlers). `src/modules/network/net_event_mgmt.c` (renamed from `net_event_app.c`) now publishes `ZEGO_UX_WIFI_STATE_CHAN` directly instead of the removed `APP_WIFI_STATE_CHAN`. See [ux-spec.md](../../../zego/bricks/ux/docs/ux-spec.md) for the current implementation. |
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-04-23-33 | Formatted Document Information: `Module` → `Project`. PRD Version updated to 2026-06-04-23-04. |
| 2026-06-04-23-00 | Initial spec — LED-only Wi-Fi state feedback using zego/led |

---

## Current state

LED 0 Wi-Fi state feedback remains owned entirely by `zego/bricks/ux` (`CONFIG_ZEGO_UX=y`,
already enabled for the startup banner — see [0-overview.md](0-overview.md)). This project's
connectivity integration point is still `src/modules/network/net_event_mgmt.c`, which publishes
`ZEGO_UX_WIFI_STATE_CHAN` on connectivity changes:

| Event | Published state |
|-------|----------------|
| `zego_on_net_event_dhcp_bound()` | `ZEGO_UX_WIFI_STATE_CONNECTED` |
| `zego_on_net_event_wifi_disconnect()` | `ZEGO_UX_WIFI_STATE_ERROR` |

Boot-time ROTATE and the LED effect state machine itself are handled internally by
`zego/bricks/ux` — see its spec for details: [ux-spec.md](../../../zego/bricks/ux/docs/ux-spec.md).

### Button 0 gesture-hook overrides (`src/modules/ux/ux.c`)

`zego/bricks/ux` also owns a second, independent `BUTTON_CHAN` listener that reacts to
`button_number == CONFIG_ZEGO_UX_BUTTON_IDX` (default `0`) and calls three `__weak` gesture
hooks — see [ux-spec.md](../../../zego/bricks/ux/docs/ux-spec.md) for the override contract.
This app's button-driven validation demos (heartbeat/CDR short-click, stack-overflow
long-press — see README.md "Buttons") are wired to `button_number == 0` on this same
physical button via their own, separate `BUTTON_CHAN` observers in `app_memfault_core.c`
and `app_memfault_nrf70_fw_stats_cdr.c`. Because zbus does not order same-priority observers
deterministically, both listener sets used to fire on every Button 0 event:

| Gesture | zego/ux default (unmodified) | This app's behavior | Resolution |
|---|---|---|---|
| Single-click | Logs current Wi-Fi mode | Heartbeat + optional nRF70 CDR trigger | **Overridden** (no-op) — README behavior only |
| Double-click | BLE-prov advertising toggle / P2P pairing | *(none)* | **Not overridden** — kept at zego/ux default |
| Long-press | Cycle Wi-Fi mode, persist to NVS, `sys_reboot()` | Stack-overflow demo crash | **Overridden** (no-op) — README behavior only, no more race with reboot |

`src/modules/ux/ux.c` provides strong definitions for `zego_ux_on_single_click()` and
`zego_ux_on_long_press()` as no-ops, so only this app's own `BUTTON_CHAN` listeners act on
those two gestures. `zego_ux_on_double_click()` is intentionally left undefined so the
zego/ux default still applies — this app has no double-click behavior of its own to protect.
