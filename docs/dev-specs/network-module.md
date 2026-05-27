# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | network |
| Version | 2026-05-26-15-21 |
| PRD Version | 2026-05-14-14-13 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/network implementation |
| 2026-05-26-15-21 | Developer Notes: document two-layer WiFi disconnect reason system (802.11 vs Zephyr status) |

---

## Overview

The network module owns Wi-Fi and IP-layer event handling and publishes runtime
connectivity state through WIFI_CHAN and NETWORK_CHAN. It is the primary source
of connectivity truth for dependent modules.

---

## Location

- Path: src/modules/network/
- Files: net_event_mgmt.c, net_event_mgmt.h, wifi_utils.c, wifi_utils.h, Kconfig, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Application module

---

## Zbus Integration

Publishes:
- WIFI_CHAN (struct wifi_msg)
- NETWORK_CHAN (struct network_msg)

No channel subscriptions.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_WIFI_MODULE | bool | y | Enable Wi-Fi/network event module |
| CONFIG_WIFI_MODULE_INIT_PRIORITY | int | 90 | SYS_INIT priority for network init |

---

## API / Public Interface

- net_event_mgmt_init()/init_network_events() initialize net_mgmt callbacks and zbus channels.
- wifi_utils helpers provide Wi-Fi info retrieval used by other modules.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| net_mgmt callback registration failure | return code check | error log and init failure path |
| zbus publish failure | return from zbus_chan_pub | warning log and continue |
| connection drop | NET_EVENT_L4_DISCONNECTED | publish disconnect and rely on connection manager retry |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | event handling and utility logic |
| RAM (static) | low | event state |
| Stack | callback context | no dedicated thread in module |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | [wifi] WiFi module initialized | module ready at boot |
| L4 connected | connected/network-ready logs | WIFI_CHAN and NETWORK_CHAN publish succeeds |
| L4 disconnected | disconnected log | disconnect event published |

---

## Developer Notes

### WiFi disconnect reason codes: two independent layers

When a WiFi disconnection occurs, the firmware logs two distinct reason values that are
frequently mistaken for a discrepancy. They originate from different protocol layers and
are **not the same namespace**.

#### Layer 1 — IEEE 802.11 reason code (wpa_supplicant)

Emitted directly by wpa_supplicant in the `CTRL-EVENT-DISCONNECTED` log line:

```
<inf> wpa_supp: wlan0: CTRL-EVENT-DISCONNECTED bssid=... reason=4 locally_generated=1
```

`reason=N` is the **IEEE 802.11 deauthentication/disassociation reason code** (IEEE Std 802.11
Table 9-45). It is placed in the management frame sent over-the-air to the AP.
`locally_generated=1` means the **device** (not the AP) initiated the frame.

| 802.11 reason code | Meaning |
|--------------------|---------|
| 1 | Unspecified |
| 2 | Previous authentication no longer valid |
| 3 | Deauthenticated — STA is leaving IBSS or ESS |
| 4 | Disassociated due to inactivity (**default for app-initiated disconnect**) |
| 6 | Class 2 frame received from non-authenticated STA |
| 7 | Class 3 frame received from non-associated STA |
| 8 | Disassociated — STA is leaving BSS |
| 15 | 4-way handshake timeout |
| 16 | Group key handshake timeout |
| 23 | IEEE 802.1X authentication failed |

> **`reason=4` for intentional disconnects is normal.** When the application calls
> `wifi_disconnect()`, wpa_supplicant sends a disassociation frame with reason code 4 by
> default regardless of why the application triggered the disconnect. The `locally_generated=1`
> flag is the definitive indicator that the disconnect was device-initiated.

#### Layer 2 — Zephyr operation status (`wifi_status.status`)

Reported by `net_event_mgmt.c` when `NET_EVENT_WIFI_DISCONNECT_RESULT` fires:

```
<wrn> net_event_mgmt: === WiFi DISCONNECTED (reason: 0) ===
```

`status` is a **`wifi_disconn_reason` enum value** — not an 802.11 reason code.
It is produced by the translation function `wpas_to_wifi_mgmt_disconn_status()` in
`zephyr/modules/hostap/src/supp_events.c`, which maps the raw 802.11 reason code to
one of five Zephyr-defined values before raising `NET_EVENT_WIFI_DISCONNECT_RESULT`.

**Critical translation rule** — wpa_supplicant `events.c` passes the 802.11 reason code
through the translation function only when the disconnect was AP-initiated. For any
locally-generated disconnect (`locally_generated=1`) the value passed is unconditionally
**0**, regardless of which 802.11 reason code appeared in the CTRL-EVENT log line:

```c
/* modules/lib/hostap/wpa_supplicant/events.c */
supplicant_send_wifi_mgmt_disc_event(wpa_s, locally_generated ? 0 : reason_code);
```

| `wifi_disconn_reason` value | Enum constant | Meaning | Source |
|-----------------------------|---------------|---------|--------|
| 0 | `WIFI_REASON_DISCONN_SUCCESS` | Device-initiated (locally_generated=1) — **always 0 regardless of 802.11 reason code** | `locally_generated ? 0 : ...` branch |
| 1 | `WIFI_REASON_DISCONN_UNSPECIFIED` | AP-initiated with unrecognised 802.11 reason, or 802.11 reason=1 | default case in `wpas_to_wifi_mgmt_disconn_status()` |
| 2 | `WIFI_REASON_DISCONN_USER_REQUEST` | *(reserved in enum; never returned by `wpas_to_wifi_mgmt_disconn_status()` in NCS v3.3.0)* | — |
| 3 | `WIFI_REASON_DISCONN_AP_LEAVING` | AP-initiated deauth, AP leaving (802.11 reason=3 `WLAN_REASON_DEAUTH_LEAVING`) | explicit case |
| 4 | `WIFI_REASON_DISCONN_INACTIVITY` | AP-initiated disassoc due to inactivity (802.11 reason=4 `WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY`) | explicit case |

> **Note on the switch statement in `net_event_mgmt.c`:** The `l2_wifi_conn_event_handler`
> disconnect switch has cases for 802.11-style values (6, 7, 8, 15, 16, 23), but since
> `wifi_status.status` carries a `wifi_disconn_reason` (max value 4), those cases can never
> be reached. They reflect a historical copy from the 802.11 table rather than Zephyr enum
> values and should not be relied upon.

#### Example: intentional application-triggered disconnect

The following three log lines are all consistent — the disconnect was device-initiated
(`locally_generated=1`), so wpa_supplicant passed `0` to the Zephyr layer, producing
`WIFI_REASON_DISCONN_SUCCESS` regardless of the 802.11 reason=4 in the supplicant log:

```
<inf> wpa_supp:           CTRL-EVENT-DISCONNECTED … reason=4 locally_generated=1
<inf> wifi_prov_over_ble: WiFi disconnected (intentional), deferring reconnect to provisioner
<wrn> net_event_mgmt:     === WiFi DISCONNECTED (reason: 0) ===
```

The 802.11 `reason=4` identifies the disassociation frame type sent over the air; the Zephyr
`reason: 0` (`WIFI_REASON_DISCONN_SUCCESS`) reflects that the disconnect was locally initiated
and completed cleanly. The `wifi_prov_over_ble` log confirms the application triggered it
intentionally. All three are consistent and expected.
