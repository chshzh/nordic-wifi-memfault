# WiFi Provisioning over BLE Module Specification

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
| 2026-07-13-11-08 | New spec (not present in legacy `pm/openspec/specs/`, which only briefly mentioned this module inside `memfault-integration.md`/`architecture.md`). Reverse-designed from current `wifi_prov_over_ble.c`. |
| 2026-07-24-11-30 | Replaced the flat 180 s reconnect fallback retry with a capped exponential backoff (5 s ×3 quick retries, then 30 → 60 → 120 → 300 s cap), mirroring the MQTT reconnect backoff pattern already used elsewhere in this app. A flat 180 s interval meant the device could sit disconnected for up to 3 minutes after the first failed attempt before trying again; the initial 5 s retry on disconnect is unchanged. `wifi_reconnect_retry_count` is reset on any successful connect or when credentials are found empty. |
| 2026-08-18-18-40 | Fixed a hang: `status == 0` on `NET_EVENT_WIFI_DISCONNECT_RESULT` was treated as "provisioner-initiated, defer reconnect" unconditionally, but the `network` module's L3 DHCP watchdog (`net_event_mgmt.c`) also produces `status == 0` from its own `NET_REQUEST_WIFI_DISCONNECT`. When that watchdog fired with no BLE client connected (e.g. after an AP power-cycle where the device re-associates but doesn't get a lease in time), reconnect was deferred to a provisioner that wasn't actually there, and the device stayed offline indefinitely. Now only defers when `current_conn != NULL` (a BLE client is actually connected and could be driving the disconnect). |
| 2026-08-18-22-15 | Fixed a second, related hang found via hardware retest of the above fix: `NET_EVENT_WIFI_CONNECT_RESULT` fires on both success *and* failure (timeout, auth failure, etc. — same event `net_event_mgmt.c` decodes into `-ETIMEDOUT`/"Connection timed out" etc.), but this module's handler treated *any* occurrence as "now connected", clearing `wifi_reconnect_pending` and cancelling the already-scheduled backoff retry. A single failed/timed-out connect attempt during the retry loop (e.g. wpa_supplicant's own 30 s association timeout racing the AP right after a power-cycle) silently killed all further retries, leaving the device offline indefinitely with no further reconnect attempts logged. Now only clears reconnect state / cancels the retry work when `status->status == 0` (genuine success). |
| 2026-08-19-15-30 | Dropped nRF54LM20DK + nRF7002EB II from Target Board(s) — that board has no board definition in NCS v2.6.4 and has been removed project-wide; see `1-architecture.md` Changelog for the full removal. No module-specific behavior changed. |

---

## Overview

Wraps the NCS BLE Wi-Fi Provisioning Service (`bluetooth/services/wifi_provisioning.h`) so a
user can set Wi-Fi credentials via the nRF Wi-Fi Provisioner mobile app. Advertises as
`PV<MAC>`, updates BLE advertisement data to reflect provisioning/connection status, and
manages Wi-Fi reconnection after a disconnect while a BLE prov session is not intentionally
driving the disconnect itself.

---

## Location

- **Path**: `src/modules/wifi_prov_over_ble/`
- **Files**: `wifi_prov_over_ble.c`, `wifi_prov_over_ble.h`, `Kconfig.wifi_prov_over_ble`, `Kconfig.defaults`, `CMakeLists.txt`

---

## Module Type

- [ ] Application module
- [x] **Library wrapper module** — wraps the Bluetooth stack + NCS Wi-Fi Provisioning Service. BLE stack and provisioning service run their own internal state; this module supplies advertisement data, a dedicated work queue for connect/reconnect handling, and net_mgmt event integration.

---

## External Library Interface

| Field | Value |
|-------|-------|
| Library | Bluetooth host stack + NCS `bt_wifi_prov` (Wi-Fi Provisioning Service) |
| NCS Kconfig | `CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED=y` (selects `BT`, `BT_PERIPHERAL`, `BT_SMP`, `BT_WIFI_PROV`, `NANOPB`) |
| Library internal threads | Bluetooth host RX/TX threads (managed by the BT stack); this module additionally owns a dedicated `adv_daemon` work queue (`K_THREAD_STACK_DEFINE(adv_daemon_stack_area, 8192)`, priority 5) for connect/reconnect work that must not run on the system work queue |

**APIs called by this module** (app → library):

```c
bt_enable(NULL);                          /* start BLE host */
bt_le_adv_start(...);                     /* start/update advertising (fast/slow params) */
bt_le_adv_update_data(ad, ..., sd, ...);   /* refresh service-data byte (prov/conn status, RSSI) */
bt_wifi_prov_state_get();                 /* query whether a provisioning session is active */
net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, ...);  /* trigger reconnect using stored credentials */
wifi_credentials_for_each_ssid(count_ssid_cb, &empty);  /* check if any credentials are stored (no
                                                            wifi_credentials_is_empty() on this NCS version) */
```

**Callbacks implemented by this module** (library → app):

```c
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                     uint32_t mgmt_event, struct net_if *iface);
/* Reacts to NET_EVENT_WIFI_DISCONNECT_RESULT / NET_EVENT_WIFI_CONNECT_RESULT,
 * but only while bt_wifi_prov_state_get() is true. */

static void wifi_connect_work_handler(struct k_work *work);
/* Scheduled on adv_daemon_work_q to retry NET_REQUEST_WIFI_CONNECT_STORED. */
```

**Zbus integration** — how library events are forwarded to the rest of the app:

| Library event / callback | Zbus channel published | Message |
|--------------------------|----------------------|---------|
| none directly | — | This module does not publish Zbus messages. It *subscribes* to `WIFI_CHAN` (published by `network`) to know when the device is connected/disconnected and to update BLE advertisement data accordingly. |

---

## Zbus Integration

**Subscribes to**: `WIFI_CHAN` — updates BLE advertisement flag bits (`ADV_DATA_FLAG_CONN_STATUS_BIT`) when Wi-Fi connects/disconnects, so the mobile app sees live connection status during provisioning.

**Publishes to**: none.

---

## State Machine

Not SMF. Event/work-queue driven:

- On boot: if `wifi_credentials_is_empty()`, starts BLE advertising immediately (`PROV_BT_LE_ADV_PARAM_FAST`).
- If credentials already exist at boot (`credentials_existed_at_boot`), the module still starts BLE (for re-provisioning) but does not force an immediate reconnect race — it defers to `network`/Connection Manager auto-connect.
- On `NET_EVENT_WIFI_DISCONNECT_RESULT` with a *non-zero* status (unintentional disconnect) while a provisioning session is active: schedules `wifi_connect_work_handler` on `adv_daemon_work_q` after `WIFI_RECONNECT_DELAY_SEC` (5 s).
- On `status == 0` (intentional/locally-generated disconnect) **while a BLE client is connected** (`current_conn != NULL`): explicitly skips auto-reconnect — the provisioning library owns that reconnect itself to avoid racing the WPA supplicant while it is driving a scan. `status == 0` is otherwise ambiguous (the `network` module's L3 DHCP watchdog also produces it via its own `NET_REQUEST_WIFI_DISCONNECT`), so without a BLE client connected the normal reconnect path below is scheduled instead.
- On `NET_EVENT_WIFI_CONNECT_RESULT`: this event fires on both success and failure (timeout, auth failure, etc.). Only `status == 0` (genuine success) clears `wifi_reconnect_pending`/`wifi_reconnect_retry_count` and cancels the pending retry work — a failed/timed-out attempt during an active retry cycle must leave the already-scheduled backoff retry running, otherwise the device is left offline with no further reconnect attempts.
- Advertisement parameters/data are periodically refreshed via `update_adv_param_work` / `update_adv_data_work` delayed work.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|--------------|
| `CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED` | bool | `n` (enabled via `prj.conf`) | Enable BLE-based Wi-Fi provisioning; selects `BT`, `BT_PERIPHERAL`, `BT_SMP`, `BT_WIFI_PROV`, `NANOPB` |
| `CONFIG_BT_WIFI_PROV_LOG_LEVEL_*` / `CONFIG_WIFI_PROV_OVER_BLE_LOG_LEVEL_*` | choice | `INF` | Log levels (set in `prj.conf`, cannot live in `Kconfig.defaults`) |
| `CONFIG_WIFI_PROV_ADV_DATA_UPDATE_INTERVAL` | int | (library default) | Interval for periodic advertisement-data refresh, only used if `CONFIG_WIFI_PROV_ADV_DATA_UPDATE` is set |

---

## API / Public Interface

```c
/* wifi_prov_over_ble.h — see file for exact declarations */
/* Module is self-contained: initializes via SYS_INIT-equivalent module init in
 * wifi_prov_over_ble.c; no functions are called by other application modules. */
```

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| BLE advertising start failure | Return code from `bt_le_adv_start()` | Logged; not currently retried automatically |
| Reconnect after unintentional disconnect | `NET_EVENT_WIFI_DISCONNECT_RESULT` status != 0 | Reconnect scheduled once (`wifi_reconnect_pending` guard prevents duplicate scheduling) after `WIFI_RECONNECT_DELAY_SEC` (5 s); if still disconnected, the fallback retry backs off 5 s ×3, then 30→60→120→300 s cap (`wifi_reconnect_backoff_sec()`) instead of a flat interval |
| Connect attempt fails mid-retry-cycle (timeout, auth failure) | `NET_EVENT_WIFI_CONNECT_RESULT` status != 0 | Retry cycle is left running (backoff work is only cancelled on `status == 0`); previously any status on this event cancelled the retry, silently ending the reconnect loop after the first failed attempt |
| Stack overflow risk in connect chain | Historical: 4096 B stack overflowed inside `zsock_poll_internal()` | Fixed by sizing `ADV_DAEMON_STACK_SIZE` to 8192 B (documented in-code) |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~45 KB | BLE stack + provisioning service (per legacy architecture estimate; not re-measured) |
| RAM (static) | ~20 KB | Per legacy estimate |
| Stack | 8192 B (`adv_daemon` work queue) | Sized for the full `NET_REQUEST_WIFI_CONNECT_STORED` call chain including `zsock_poll_internal()` |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| No stored credentials at boot | BLE advertising starts as `PV<MAC>` | `wifi_credentials_is_empty()` true |
| Provisioning success | Wi-Fi connects after credentials received | Credentials written via `net/wifi_credentials`, Connection Manager connects |
| Unintentional disconnect during prov session | `WiFi disconnected, scheduling reconnect` | `status != 0` while `bt_wifi_prov_state_get()` true |
| Intentional disconnect (provisioner scan) | `WiFi disconnected (intentional), deferring reconnect to provisioner` | `status == 0` with a BLE client connected |
| L3 watchdog forces disconnect (associated, no IP after timeout) with no BLE client connected | `WiFi disconnected, scheduling reconnect` (not deferred) | `status == 0` with `current_conn == NULL` — regression test: power-cycle the AP mid-session and confirm the device reconnects instead of hanging offline |
| A connect attempt inside the retry loop times out / fails to associate | Retry cycle keeps logging `WiFi still disconnected, retrying in N seconds` past the failed attempt | `NET_EVENT_WIFI_CONNECT_RESULT` status != 0 does not cancel `wifi_connect_work` — regression test: power-cycle the AP and confirm reconnect eventually succeeds even if the first association attempt times out (do not stop observing after the first `Connection timed out` log line) |

---

## Open Issues / TBD

- [ ] BLE advertising start failures are logged but not retried.
- [ ] No LED indication of provisioning/connection state (see PRD §8 Out of Scope — no LED module yet).

---

## Related Specs

- [network-module.md](network-module.md) — publishes `WIFI_CHAN`, owns the actual Wi-Fi connect/disconnect state machine
- [1-architecture.md](1-architecture.md) — Zbus channel table

*(Changelog is maintained at the top of this document.)*
