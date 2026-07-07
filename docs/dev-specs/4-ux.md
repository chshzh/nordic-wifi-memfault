# UX Module Engineering Spec — LED Wi-Fi Feedback

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-memfault |
| Version | 2026-07-07-16-32 |
| PRD Version | 2026-07-07-16-32 |
| NCS Version | v3.4.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Removed — superseded by `zego/bricks/ux` |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-07-16-32 | **Module removed.** PRD Version updated to 2026-07-07-16-32. The local `src/modules/ux/` (`CONFIG_APP_UX_MODULE`) duplicated LED 0 Wi-Fi-state feedback now provided by `zego/bricks/ux` (`CONFIG_ZEGO_UX`), which this project already links in for the startup banner. Both modules were independently driving LED 0 (confirmed on hardware: duplicate "LED ROTATE effect started" at boot from two separate `SYS_INIT` handlers). `src/modules/network/net_event_mgmt.c` (renamed from `net_event_app.c`) now publishes `ZEGO_UX_WIFI_STATE_CHAN` directly instead of the removed `APP_WIFI_STATE_CHAN`. See [ux-spec.md](../../../zego/bricks/ux/docs/ux-spec.md) for the current implementation. |
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-04-23-33 | Formatted Document Information: `Module` → `Project`. PRD Version updated to 2026-06-04-23-04. |
| 2026-06-04-23-00 | Initial spec — LED-only Wi-Fi state feedback using zego/led |

---

## Current state

LED 0 Wi-Fi state feedback is now owned entirely by `zego/bricks/ux` (`CONFIG_ZEGO_UX=y`,
already enabled for the startup banner — see [0-overview.md](0-overview.md)). This project's
only remaining integration point is `src/modules/network/net_event_mgmt.c`, which publishes
`ZEGO_UX_WIFI_STATE_CHAN` on connectivity changes:

| Event | Published state |
|-------|----------------|
| `zego_on_net_event_dhcp_bound()` | `ZEGO_UX_WIFI_STATE_CONNECTED` |
| `zego_on_net_event_wifi_disconnect()` | `ZEGO_UX_WIFI_STATE_ERROR` |

Boot-time ROTATE and the LED effect state machine itself are handled internally by
`zego/bricks/ux` — see its spec for details: [ux-spec.md](../../../zego/bricks/ux/docs/ux-spec.md).

Button gestures (heartbeat, OTA check, crash demo) remain handled separately by
`app_memfault_core.c` via `BUTTON_CHAN`, unaffected by this removal.
