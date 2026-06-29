# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-29-13-23 |
| PRD Version | 2026-06-19-12-31 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Implemented |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-29-13-23 | Added `wifi_disconnect_reason_handler` net_mgmt callback; added `CONFIG_WIFI_NM_WPA_SUPPLICANT_BSS_MAX_IDLE_TIME=30` Kconfig. |
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-04-23-45 | Trimmed to app-layer shim only: updated Zbus Integration to show all channels and weak hook override table; removed stale Module Type, API, Kconfig, Memory, Test Points, and Developer Notes sections. |
| 2026-06-04-23-33 | Location section updated to reference `net_event_app.c`; zego/network canonical spec banner added. |
| 2026-05-26-15-21 | Developer Notes: document two-layer WiFi disconnect reason system (802.11 vs Zephyr status) |

---

## Overview

The network module owns Wi-Fi and IP-layer event handling and publishes runtime
connectivity state through WIFI_CHAN and NETWORK_CHAN. It is the primary source
of connectivity truth for dependent modules.

> **Core event handling provided by zego/network.**
>
> This spec covers only the application-layer shim (`net_event_app.c`) that bridges
> `zego/network` events to the application zbus channels (`WIFI_CHAN`, `NETWORK_CHAN`,
> `APP_WIFI_STATE_CHAN`). The core Wi-Fi/network event loop is in `zego/modules/network`.
>
> Canonical spec: **[zego/network ↗](https://github.com/chshzh/zego/blob/main/modules/network/docs/network-spec.md)**

---

## Location

- Path: `src/modules/network/`
- Files: `net_event_app.c`, `Kconfig`, `Kconfig.defaults`, `CMakeLists.txt`

---

## Channel Definitions

| Channel | Type | Defined in |
|---------|------|------------|
| `NETWORK_CHAN` | `struct network_msg` | `net_event_app.c` |
| `APP_WIFI_STATE_CHAN` | `struct app_wifi_state_msg` | `net_event_app.c` |

---

## Weak Hook Overrides

### `zego_network_on_wifi_connected(mode, ip, mac, ssid)`

| Publishes | Message | Condition |
|-----------|---------|----------|
| `WIFI_CHAN` | `{ .type = WIFI_STA_CONNECTED }` | `CONFIG_ZEGO_WIFI_BLE_PROV=y` only |
| `NETWORK_CHAN` | `{ .type = NETWORK_READY, .ready = true }` | Always |
| `APP_WIFI_STATE_CHAN` | `{ .state = APP_WIFI_STATE_CONNECTED, .mode = mode }` | Always |

### `zego_network_on_wifi_disconnected()`

| Publishes | Message | Condition |
|-----------|---------|----------|
| `WIFI_CHAN` | `{ .type = WIFI_STA_DISCONNECTED }` | `CONFIG_ZEGO_WIFI_BLE_PROV=y` only |
| `NETWORK_CHAN` | `{ .type = NETWORK_NOT_READY, .ready = false }` | Always |
| `APP_WIFI_STATE_CHAN` | `{ .state = APP_WIFI_STATE_ERROR, .mode = ZEGO_WIFI_MODE_STA }` | Always |

---

## Disconnect Reason Diagnostic Callback

`net_event_app.c` registers an additional `NET_EVENT_WIFI_DISCONNECT_RESULT` net_mgmt
callback (`wifi_disconnect_reason_handler`) via `SYS_INIT` at `APPLICATION` priority.

**Purpose:** Zephyr's `wifi_status.disconn_reason` (enum `wifi_disconn_reason`) maps all
802.11 reason codes that have no explicit entry to `WIFI_REASON_DISCONN_UNSPECIFIED (1)`,
hiding root causes such as reason=6 (AP table cleared), reason=8 (AP radio restart), and
reason=34 (Low ACK / power-save). The raw 802.11 code appears only in the preceding
`wpa_supp: wlan0: CTRL-EVENT-DISCONNECTED reason=N` log line, which requires
`CONFIG_WIFI_NM_WPA_SUPPLICANT_LOG_LEVEL_INF=y` (already set in `prj.conf`).

The callback logs the Zephyr reason value with an explicit pointer to the wpa_supplicant
CTRL-EVENT line, making CDR log analysis unambiguous.

**Kconfig dependency:** `CONFIG_WIFI_NM_WPA_SUPPLICANT_BSS_MAX_IDLE_TIME=30` (set in
`prj.conf`, default was 300 s). The STA includes this value in its association request,
asking the AP to advertise a 30 s BSS Max Idle Period. When the AP honors the request, the
WNM keep-alive null-data frame fires every ~30 s, surfacing AP-side STA table clears
(reason=6) in seconds rather than minutes.

> **Board note:** `boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf` overrides this to `0`
> (disabled). The nRF70 on nRF54LM20DK is SPI-connected (`spi@c8000`) and the WNM
> keep-alive L2 traffic immediately after association destabilises the SPI path on that
> board, causing the application to miss the DHCP-bound event. BSS Max Idle is therefore
> only active on nRF7002DK.

---

## Related Specs

- [architecture.md](architecture.md) — zbus channel map, SYS_INIT priorities
- [zego/network ↗](https://github.com/chshzh/zego/blob/main/modules/network/docs/network-spec.md) — Wi-Fi event handling, connection state machine, disconnect reason codes, Kconfig


