# WiFi Provisioning over BLE Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-08-20-14-56 |
| PRD Version | 2026-08-20-14-56 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Status | Implemented |

> `Version` = this spec's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this spec tracks. The two normally **differ** — never set them equal.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-08-20-14-56 | Wording fix only, no behavior change here: dropped stale "backoff" wording from the Overview now that `network` module's retry is a flat interval again (PRD v2026-08-20-14-56) — see [network-module.md](network-module.md) Changelog for the actual retry-logic simplification. |
| 2026-08-20-14-47 | **STA reconnect ownership moved to `network` module** (PRD v2026-08-20-14-47) — this module no longer owns any Wi-Fi reconnect/auto-connect logic. Removed `wifi_mgmt_event_handler()`, `wifi_connect_work`/`wifi_connect_work_handler()`, the reconnect state/backoff function, and the dedicated `adv_daemon_work_q` (8192 B) work queue entirely (relocated, not duplicated, into `network/net_event_mgmt.c` as `net_connect_work_q`). This module now only: publishes a new `BLE_CHAN` channel (`BLE_CLIENT_CONNECTED`/`BLE_CLIENT_DISCONNECTED`) from the BT `connected()`/`disconnected()` callbacks, so `network` can still defer its own reconnect attempt while a BLE provisioning session is active; and calls the new `net_event_mgmt_request_connect()` after fresh credentials are provisioned, instead of scheduling its own connect work. The remaining lightweight advertisement work (`update_adv_param_work`/`update_adv_data_work`) now runs on the system work queue instead of the removed dedicated queue. Rationale: reconnect/boot auto-connect must work regardless of `CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED` (e.g. when credentials are entered via `wifi cred shell` instead of BLE provisioning) — see [network-module.md](network-module.md) for the full account. Build-verified clean on nRF7002DK (RAM 99.55%, unchanged within 8 B noise). |
| 2026-08-20-13-20 | **Zbus event redesign**: switched from subscribing to `WIFI_CHAN`'s `WIFI_STA_CONNECTED`/`WIFI_STA_DISCONNECTED` to `NETWORK_CHAN`'s `NETWORK_READY`/`NETWORK_NOT_READY` for the BLE advertisement status flag. See [network-module.md](network-module.md) for the full rationale. |
| 2026-07-13-11-08 | New spec (not present in legacy `pm/openspec/specs/`, which only briefly mentioned this module inside `memfault-integration.md`/`architecture.md`). Reverse-designed from current `wifi_prov_over_ble.c`. |
| 2026-07-24-11-30 | Replaced the flat 180 s reconnect fallback retry with a capped exponential backoff (5 s ×3 quick retries, then 30 → 60 → 120 → 300 s cap), mirroring the MQTT reconnect backoff pattern already used elsewhere in this app. A flat 180 s interval meant the device could sit disconnected for up to 3 minutes after the first failed attempt before trying again; the initial 5 s retry on disconnect is unchanged. `wifi_reconnect_retry_count` is reset on any successful connect or when credentials are found empty. |
| 2026-08-18-18-40 | Fixed a hang: `status == 0` on `NET_EVENT_WIFI_DISCONNECT_RESULT` was treated as "provisioner-initiated, defer reconnect" unconditionally, but the `network` module's L3 DHCP watchdog (`net_event_mgmt.c`) also produces `status == 0` from its own `NET_REQUEST_WIFI_DISCONNECT`. When that watchdog fired with no BLE client connected (e.g. after an AP power-cycle where the device re-associates but doesn't get a lease in time), reconnect was deferred to a provisioner that wasn't actually there, and the device stayed offline indefinitely. Now only defers when `current_conn != NULL` (a BLE client is actually connected and could be driving the disconnect). |
| 2026-08-18-22-15 | Fixed a second, related hang found via hardware retest of the above fix: `NET_EVENT_WIFI_CONNECT_RESULT` fires on both success *and* failure (timeout, auth failure, etc. — same event `net_event_mgmt.c` decodes into `-ETIMEDOUT`/"Connection timed out" etc.), but this module's handler treated *any* occurrence as "now connected", clearing `wifi_reconnect_pending` and cancelling the already-scheduled backoff retry. A single failed/timed-out connect attempt during the retry loop (e.g. wpa_supplicant's own 30 s association timeout racing the AP right after a power-cycle) silently killed all further retries, leaving the device offline indefinitely with no further reconnect attempts logged. Now only clears reconnect state / cancels the retry work when `status->status == 0` (genuine success). |
| 2026-08-19-15-30 | Dropped nRF54LM20DK + nRF7002EB II from Target Board(s) — that board has no board definition in NCS v2.6.4 and has been removed project-wide; see `1-architecture.md` Changelog for the full removal. No module-specific behavior changed. |

---

## Overview

Wraps the NCS BLE Wi-Fi Provisioning Service (`bluetooth/services/wifi_provisioning.h`) so a
user can set Wi-Fi credentials via the nRF Wi-Fi Provisioner mobile app. Advertises as
`PV<MAC>` and updates BLE advertisement data to reflect provisioning/connection status.
Wi-Fi STA reconnect (retry + boot-time auto-connect) is owned entirely by the
`network` module now — this module's only involvement is publishing `BLE_CHAN` so `network`
can avoid racing an active BLE provisioning session's own connect attempt, and requesting an
immediate connect attempt right after fresh credentials are provisioned.

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
| Library internal threads | Bluetooth host RX/TX threads (managed by the BT stack); this module's own delayed work (`update_adv_param_work`/`update_adv_data_work`) runs on the system work queue — the dedicated `adv_daemon` work queue was removed and relocated into `network/net_event_mgmt.c` for the reconnect work chain that actually needed its 8192 B stack (see [network-module.md](network-module.md)) |

**APIs called by this module** (app → library):

```c
bt_enable(NULL);                          /* start BLE host */
bt_le_adv_start(...);                     /* start/update advertising (fast/slow params) */
bt_le_adv_update_data(ad, ..., sd, ...);   /* refresh service-data byte (prov/conn status, RSSI) */
bt_wifi_prov_state_get();                 /* query whether a provisioning session is active */
wifi_utils_has_stored_credentials();      /* shared helper (network/wifi_utils.h) — check if any
                                              credentials are stored */
net_event_mgmt_request_connect();         /* request an immediate connect attempt after fresh
                                              provisioning (network/net_event_mgmt.h) */
```

**Callbacks implemented by this module** (library → app):

```c
static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
/* BT_CONN_CB_DEFINE callbacks; publish BLE_CLIENT_CONNECTED/BLE_CLIENT_DISCONNECTED
 * on BLE_CHAN in addition to their existing advertisement-refresh duties. */
```

**Zbus integration** — how library events are forwarded to the rest of the app:

| Library event / callback | Zbus channel published | Message |
|--------------------------|----------------------|---------|
| BT `connected()` | `BLE_CHAN` | `BLE_CLIENT_CONNECTED` |
| BT `disconnected()` | `BLE_CHAN` | `BLE_CLIENT_DISCONNECTED` |

It also *subscribes* to `NETWORK_CHAN` (published by `network`) to know when the device is
connected/disconnected and to update BLE advertisement data accordingly.

---

## Zbus Integration

**Subscribes to**: `NETWORK_CHAN` — updates BLE advertisement flag bits (`ADV_DATA_FLAG_CONN_STATUS_BIT`) when the network becomes ready/not-ready, so the mobile app sees live connection status during provisioning.

**Publishes to**: `BLE_CHAN` — `BLE_CLIENT_CONNECTED`/`BLE_CLIENT_DISCONNECTED`, from the BT `connected()`/`disconnected()` callbacks. Consumed by `network` to defer its own STA reconnect attempt while a BLE provisioning session is active.

---

## State Machine

Not SMF. Event/work-queue driven:

- On boot: if `wifi_utils_has_stored_credentials()` is false, starts BLE advertising immediately (`PROV_BT_LE_ADV_PARAM_FAST`).
- If credentials already exist at boot (`credentials_existed_at_boot`), the module still starts BLE (for re-provisioning) but does not force an immediate connect request — `network`'s own boot-time auto-connect (see [network-module.md](network-module.md)) handles that.
- On BT `connected()`/`disconnected()`: publishes `BLE_CLIENT_CONNECTED`/`BLE_CLIENT_DISCONNECTED` on `BLE_CHAN` (in addition to the existing advertisement-refresh duties), so `network` knows whether a BLE client is actively connected and could be driving its own Wi-Fi scan/connect.
- On fresh Wi-Fi credentials being provisioned (`current_prov_state && !last_prov_state` transition, then a `WiFi credentials provisioned` check in `update_wifi_status_in_adv()`): calls `net_event_mgmt_request_connect()` instead of scheduling its own connect work.
- All Wi-Fi reconnect/backoff logic (disconnect handling, retry cancellation on success, etc.) now lives entirely in `network/net_event_mgmt.c` — see that spec's State Machine and Changelog for the reconnect-ownership move and the two hardware-bug-fix regressions it must continue to pass.
- Advertisement parameters/data are periodically refreshed via `update_adv_param_work` / `update_adv_data_work` delayed work, now on the system work queue.

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

Wi-Fi reconnect error handling (disconnect backoff, connect-failure-mid-retry, the historical
connect-chain stack overflow fix, etc.) moved entirely to [network-module.md](network-module.md)
— see that spec's Error Handling table.

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~45 KB | BLE stack + provisioning service (per legacy architecture estimate; not re-measured) |
| RAM (static) | ~20 KB | Per legacy estimate |
| Stack | none dedicated | The 8192 B `adv_daemon` work queue was relocated (not duplicated) into `network/net_event_mgmt.c` as `net_connect_work_q`, since that module now owns the `NET_REQUEST_WIFI_CONNECT_STORED` call chain it was sized for. Remaining advertisement work runs on the system work queue. |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| No stored credentials at boot | BLE advertising starts as `PV<MAC>` | `wifi_utils_has_stored_credentials()` false |
| Provisioning success | Wi-Fi connects after credentials received | Credentials written via `net/wifi_credentials`, `network` connects via `net_event_mgmt_request_connect()` |
| BT client connects/disconnects during provisioning | `BLE_CHAN`: `BLE_CLIENT_CONNECTED` / `BLE_CLIENT_DISCONNECTED` | On `connected()`/`disconnected()` BT callbacks |

Wi-Fi reconnect/backoff test points (unintentional disconnect, intentional-disconnect
deferral, L3-watchdog regression, connect-failure-mid-retry regression) moved to
[network-module.md](network-module.md) — see that spec's Test Points table.

---

## Open Issues / TBD

- [ ] BLE advertising start failures are logged but not retried.
- [ ] No LED indication of provisioning/connection state (see PRD §8 Out of Scope — no LED module yet).

---

## Related Specs

- [network-module.md](network-module.md) — publishes `NETWORK_CHAN`, owns the actual Wi-Fi connect/disconnect state machine and all STA reconnect logic; subscribes to this module's `BLE_CHAN`
- [1-architecture.md](1-architecture.md) — Zbus channel table

*(Changelog is maintained at the top of this document.)*
