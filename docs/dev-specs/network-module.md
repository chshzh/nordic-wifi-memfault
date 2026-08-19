# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-08-19-15-30 |
| PRD Version | 2026-08-19-15-00 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Status | Implemented |

> `Version` = this spec's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this spec tracks. The two normally **differ** — never set them equal.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Replaces `pm/openspec/specs/wifi-module.md`. The legacy `wifi/wifi.c` module was renamed/split into `network/net_event_mgmt.c` (L2/L3 event handling, Zbus publishing, SoftAP event handlers) and `network/wifi_utils.c` (mode/channel/credential helper functions). The previously-documented 1-second delayed boot connect no longer exists in `net_event_mgmt.c` — connection is now driven purely by Connection Manager / Wi-Fi mgmt events, no artificial startup delay. |
| 2026-07-24-11-30 | Added an L3 DHCP-bound watchdog (`CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC`, default 30 s, 0 = disabled), ported from the more complete `zego/bricks/network` reference brick. A successful `NET_EVENT_WIFI_CONNECT_RESULT` only means L2 association succeeded, not that an IP was obtained — without this, a device that associates but never gets a DHCP lease (or loses its lease while still linked) sat "associated, no IP" forever with no recovering event. The watchdog is armed on L2 connect success and on lease loss (`NET_EVENT_IPV4_ADDR_DEL`), cancelled on `NET_EVENT_IPV4_DHCP_BOUND` and `NET_EVENT_WIFI_DISCONNECT_RESULT`; on expiry it issues `NET_REQUEST_WIFI_DISCONNECT`, which produces a `DISCONNECT_RESULT` that re-arms whichever module owns STA reconnect (`wifi_prov_over_ble`). |
| 2026-08-19-15-30 | Dropped nRF54LM20DK + nRF7002EB II from Target Board(s) — that board has no board definition in NCS v2.6.4 and has been removed project-wide; see `1-architecture.md` Changelog for the full removal. No module-specific behavior changed. |

---

## Overview

The `network` module owns all Wi-Fi and IP-layer event handling: registering `net_mgmt`
callbacks for interface (L2), Wi-Fi connect/disconnect (L2), WPA supplicant readiness (L3),
IPv4/DHCP (L3), and (scaffolding only) SoftAP station events; publishing `WIFI_CHAN` and
`NETWORK_CHAN` Zbus events for the rest of the app; and exposing small Wi-Fi helper
utilities (mode/channel setting, credential bootstrap, TX-injection mode) used by other
modules and by future SoftAP/diagnostic work. It does **not** implement SoftAP or Wi-Fi
Direct as a selectable mode in this release — only STA is active (see PRD §2.1, §8).

---

## Location

- **Path**: `src/modules/network/`
- **Files**: `net_event_mgmt.c`, `net_event_mgmt.h`, `wifi_utils.c`, `wifi_utils.h`, `Kconfig`, `Kconfig.defaults`, `CMakeLists.txt`

---

## Module Type

- [x] **Application module** — event-driven (not SMF); reacts to `net_mgmt` callbacks rather than a message queue.
- [ ] Library wrapper module

---

## Zbus Integration

**Subscribes to**: none.

**Publishes to**: `WIFI_CHAN` and `NETWORK_CHAN`.

```c
struct wifi_msg {
	enum wifi_msg_type type;  /* WIFI_STA_CONNECTED, WIFI_STA_DISCONNECTED, WIFI_DNS_READY, WIFI_ERROR */
	int32_t rssi;              /* not currently populated (always 0) */
	int error_code;
};

struct network_msg {
	enum network_msg_type type;  /* NETWORK_READY, NETWORK_NOT_READY */
	bool ready;
};
```

`WIFI_STA_CONNECTED` / `WIFI_STA_DISCONNECTED` are the events other modules react to
(`app_memfault`, `wifi_prov_over_ble`, `app_https_client`, `app_mqtt_client`, OTA triggers).
`WIFI_ERROR` is published on `NET_EVENT_WIFI_CONNECT_RESULT` failure. `NETWORK_CHAN` is
published for forward compatibility (IP-layer readiness) but currently has no subscribers.

---

## State Machine

Not SMF. Event-driven via five `net_mgmt_event_callback` registrations, each masked to a
layer:

```mermaid
stateDiagram-v2
    [*] --> WaitIfUp
    WaitIfUp --> WaitWpaReady: NET_EVENT_IF_UP [iface_up_sem given]
    WaitWpaReady --> Associating: NET_EVENT_WPA_SUPP_READY [wpa_supplicant_ready_sem given]
    Associating --> Connected: NET_EVENT_WIFI_CONNECT_RESULT [status==0] / publish WIFI_STA_CONNECTED
    Associating --> ConnError: NET_EVENT_WIFI_CONNECT_RESULT [status!=0] / publish WIFI_ERROR
    Connected --> DhcpBound: NET_EVENT_IPV4_DHCP_BOUND [ipv4_dhcp_bond_sem given] / publish NETWORK_READY, cancel L3 watchdog
    DhcpBound --> Disconnected: NET_EVENT_WIFI_DISCONNECT_RESULT / publish WIFI_STA_DISCONNECTED
    Disconnected --> Associating: reconnect (Connection Manager / stored-credential auto-connect)
    ConnError --> Associating: supplicant auto-retries (status 1, 2, 16) or app retries
```

**State descriptions:**

| State | Description | Notes |
|-------|-------------|-------|
| WaitIfUp | Waiting for the Wi-Fi network interface to come up | `k_sem` (`iface_up_sem`) given on `NET_EVENT_IF_UP` |
| WaitWpaReady | Waiting for the WPA supplicant to signal readiness | `wpa_supplicant_ready_sem`; NCS v2.6.4 names this event `NET_EVENT_WPA_SUPP_READY` (renamed to `NET_EVENT_SUPPLICANT_READY` in later NCS) |
| Associating | Connection attempt in progress (via `wifi_credentials`/Connection Manager, or triggered by `wifi_prov_over_ble`) | Decodes and logs common WPA status codes (auth failure, AP not found, timeout, etc.) |
| Connected | L2 association succeeded | `wifi_print_status()` called; `WIFI_STA_CONNECTED` published; if `CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC > 0`, the L3 DHCP-bound watchdog is armed here |
| DhcpBound | IP address assigned | `ipv4_dhcp_bond_sem` given; `NETWORK_READY` published on `NETWORK_CHAN`; L3 watchdog cancelled |
| Disconnected | L2/L4 disconnected | `WIFI_STA_DISCONNECTED` published; decodes common 802.11 disconnect reason codes; L3 watchdog cancelled |
| ConnError | Connection attempt failed | `WIFI_ERROR` published with the raw status code |

> SoftAP event handling (`l2_wifi_softap_event_handler`, station connect/disconnect tracking,
> per-station IP bookkeeping) exists in `net_event_mgmt.c` behind
> `#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)` but this Kconfig is not selected in this
> project — the code compiles out. Not modeled above; see PRD §8 Out of Scope.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|--------------|
| `CONFIG_WIFI_MODULE` | bool | `y` | Enable Wi-Fi and network event management |
| `CONFIG_WIFI_MODULE_INIT_PRIORITY` | int | `90` | `SYS_INIT` priority for `init_network_events` |
| `CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC` | int (0–300) | `30` (0 = disabled) | L3 connectivity watchdog: forces `NET_REQUEST_WIFI_DISCONNECT` if DHCP hasn't bound this many seconds after L2 association, or a lease is lost while still associated |
| `CONFIG_SOFTAP_SSID` | string | `"device_AP"` | SoftAP SSID — unused while `WIFI_NM_WPA_SUPPLICANT_AP` is not selected |
| `CONFIG_SOFTAP_PASSWORD` | string | `"password@123"` | SoftAP password — unused (also: default is a non-secret placeholder, must not be used verbatim if SoftAP is ever enabled) |
| `CONFIG_SOFTAP_CHANNEL` | int | `1` | SoftAP channel — unused |
| `CONFIG_SOFTAP_BAND_2_4_GHZ` / `CONFIG_SOFTAP_BAND_5_GHZ` | choice | `2_4_GHZ` | SoftAP band — unused |
| `CONFIG_SOFTAP_REG_DOMAIN` | string | `"00"` | SoftAP regulatory domain — unused |
| `CONFIG_WIFI_NRF700X` | bool | `y if WIFI_MODULE` | nRF70 driver (named `WIFI_NRF70` in later NCS releases) |
| `CONFIG_WPA_SUPP` | bool | `y if WIFI_MODULE` | nRF wrapper WPA supplicant (NCS v2.6.4 name; Zephyr's `WIFI_NM_WPA_SUPPLICANT` is not used) |
| `CONFIG_WPA_SUPP_THREAD_STACK_SIZE` | int | `8192 if WIFI_MODULE` | WPA supplicant thread stack |
| `CONFIG_WPA_SUPP_WQ_STACK_SIZE` | int | `5632 if WIFI_MODULE` | WPA supplicant workqueue stack |
| `CONFIG_NRF_WIFI_SCAN_MAX_BSS_CNT` | int | `20 if WIFI_MODULE` | Max scan results |
| `CONFIG_NRF700X_RX_NUM_BUFS` | int | `16 if WIFI_MODULE` | RX buffer count |
| `CONFIG_NRF700X_MAX_TX_AGGREGATION` | int | `4 if WIFI_MODULE` | TX aggregation |
| `CONFIG_NRF700X_SR_COEX` | bool | `y if WIFI_MODULE` | Wi-Fi/BLE coexistence |
| `CONFIG_NET_CONNECTION_MANAGER` | bool | `y if WIFI_MODULE` | Connection Manager |
| `CONFIG_NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | int | `7168 if WIFI_MODULE` | Sized 1.5× a measured 4572 B (88%) high-water mark |
| `CONFIG_L2_WIFI_CONNECTIVITY` | bool | `n` (forced) | Disabled — combined with this app's Kconfig graph on NCS v2.6.4 it triggers a `net_if.h` macro-expansion bug; `network`'s own manual connect/reconnect logic makes it redundant anyway |

---

## API / Public Interface

```c
/* net_event_mgmt.h */
int init_network_events(void);
bool net_event_mgmt_is_connected(void);
extern struct k_sem iface_up_sem;
extern struct k_sem wpa_supplicant_ready_sem;
extern struct k_sem ipv4_dhcp_bond_sem;
#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
extern struct k_sem station_connected_sem;
#endif

/* wifi_utils.h */
const char *wifi_utils_get_last_ssid(void);
int wifi_utils_ensure_gateway_softap_credentials(void);   /* SoftAP-only helper, currently unused */
int wifi_utils_auto_connect_stored(void);
int wifi_set_mode(int mode);
int wifi_set_channel(int channel);
int wifi_set_tx_injection_mode(void);
#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
int wifi_set_reg_domain(void);
#endif
```

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| Wi-Fi connect failure | `NET_EVENT_WIFI_CONNECT_RESULT` with non-zero `status` | Logs decoded reason (auth failure, AP not found, timeout, etc.); publishes `WIFI_ERROR`; supplicant auto-retries on transient codes (1, 2, 16) |
| Wi-Fi disconnect | `NET_EVENT_WIFI_DISCONNECT_RESULT` | Logs decoded 802.11 reason code; publishes `WIFI_STA_DISCONNECTED` |
| Interface not found | `net_if_get_first_wifi()` returns NULL in `wifi_utils.c` helpers | Log error, return `-ENODEV` |
| Invalid channel | `wifi_set_channel()` range check | Log error, return `-EINVAL` |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~120 KB | Includes Wi-Fi driver + WPA supplicant link-in triggered by this module's Kconfig selects (not this module's own code size) |
| RAM (static) | ~60 KB | Network buffers, WPA supplicant firmware state |
| Stack | 8192 B (`WPA_SUPP_THREAD_STACK_SIZE`) + 5632 B (`WPA_SUPP_WQ_STACK_SIZE`) + 7168 B (`NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE`) | External-library threads triggered by this module, not this module's own thread (module itself has no dedicated thread) |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Interface up | `[IF] Network interface %s is up` | On `NET_EVENT_IF_UP` |
| Wi-Fi connected | `[WiFi] WiFi is connected!` | `NET_EVENT_WIFI_CONNECT_RESULT` status == 0 |
| Wi-Fi disconnected | `=== WiFi DISCONNECTED (reason: %d) ===` | `NET_EVENT_WIFI_DISCONNECT_RESULT` |
| Connect failure | `[WiFi] Reason: <decoded reason>` | `NET_EVENT_WIFI_CONNECT_RESULT` status != 0 |

---

## Open Issues / TBD

- [ ] `wifi_msg.rssi` is never populated (always 0) — future enhancement per PRD.
- [ ] SoftAP support (Kconfig + `net_event_mgmt.c` handlers) is present but dormant; decide whether to complete or remove (tracked in `0-overview.md` Open Issues #2).
- [ ] `CONFIG_SOFTAP_PASSWORD` default (`"password@123"`) is a placeholder and must never be used verbatim if SoftAP is enabled in the future.

---

## Related Specs

- [1-architecture.md](1-architecture.md) — Zbus channel table, boot sequence
- [app-wifi-prov-ble-module.md](app-wifi-prov-ble-module.md) — credential provisioning, consumes `WIFI_CHAN`
- [app-memfault-module.md](app-memfault-module.md) — consumes `WIFI_CHAN` for upload-on-connect

*(Changelog is maintained at the top of this document.)*
