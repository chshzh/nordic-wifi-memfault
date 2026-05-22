# App Wi-Fi Provisioning over BLE Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | wifi_prov_over_ble |
| Version | 2026-05-22-10-00 |
| PRD Version | 2026-05-22-10-00 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/wifi_prov_over_ble implementation |

---

## Overview

This module wraps Nordic Wi-Fi provisioning service over BLE to allow mobile app
credential onboarding. Credentials are stored persistently and used by the
network module for automatic reconnection on later boots.

---

## Location

- Path: src/modules/wifi_prov_over_ble/
- Files: wifi_prov_over_ble.c, wifi_prov_over_ble.h, Kconfig.wifi_prov_over_ble, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Library wrapper module

---

## External Library Interface

| Field | Value |
|-------|-------|
| Library | Nordic Wi-Fi provisioning over BLE service |
| NCS Kconfig | CONFIG_BT_WIFI_PROV and selected dependencies via CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED |
| Library internal threads | BLE/controller internal workqueues |

APIs called by this module include BLE/provisioning start-stop lifecycle and
provisioning callback registration provided by NCS provisioning libraries.

Callbacks implemented handle provisioning status transitions and credential
write completion.

Zbus integration:
- Subscribes to WIFI_CHAN for connectivity state changes to adjust advertising and user feedback behavior.

---

## Zbus Integration

Subscribes to WIFI_CHAN and reacts to:
- WIFI_STA_CONNECTED
- WIFI_STA_DISCONNECTED

Does not publish a dedicated channel in current implementation.

---

## Reconnect and Multi-AP Credential Rotation

On a connection failure or unexpected disconnect, the module schedules retries via
`wifi_connect_work` (a delayable k_work item). Each retry invokes
`connect_stored_rotating()` instead of `NET_REQUEST_WIFI_CONNECT_STORED`.

### Why the replacement is needed

`NET_REQUEST_WIFI_CONNECT_STORED` (Zephyr's `connect_stored_command`) internally
iterates stored credentials with `wifi_credentials_for_each_ssid` but stops as soon as
one `NET_REQUEST_WIFI_CONNECT` call returns 0 (request accepted). This means it always
tries only the first stored credential and skips every subsequent one.

### Credential rotation implementation

| Symbol | Type | Role |
|--------|------|------|
| `cred_rotate_idx` | `static int` | Index of the next credential to try; wraps modulo stored count |
| `connect_stored_rotating()` | function | Counts stored credentials, picks `cred_rotate_idx % count`, calls `NET_REQUEST_WIFI_CONNECT` directly, then advances `cred_rotate_idx` |
| `count_stored_creds()` | callback | Counts entries via `wifi_credentials_for_each_ssid` |
| `connect_nth_cred()` | callback | Loads the Nth credential with `wifi_credentials_get_by_ssid_personal_struct` and submits `NET_REQUEST_WIFI_CONNECT` |

### Retry timing (2 stored credentials example)

| Event | Credential tried | cred_rotate_idx after |
|-------|-----------------|----------------------|
| Boot auto-connect (l2_wifi_conn — not controlled by this module) | first stored | — |
| Retry 1 (T+5 s) | idx 0 → times out → advances to 1 | 1 |
| Retry 2 (T+185 s) | idx 1 → connects | 0 |
| Next disconnect → Retry 1 (T+5 s) | idx 0 | 1 |

Timing constants: `WIFI_RECONNECT_DELAY_SEC = 5`, `WIFI_RECONNECT_RETRY_SEC = 180`.

### Retry plan logging

When the retry loop first starts (`wifi_reconnect_pending` transitions false → true),
`log_retry_plan()` emits:

```
[wifi_prov_over_ble] --- Retry schedule (2 stored network(s)) ---
[wifi_prov_over_ble]   T+5s [0] EX75_5G
[wifi_prov_over_ble]   T+185s [1] BE92U_5G
[wifi_prov_over_ble] >>> Tip: Open 'nRF Wi-Fi Provisioner' BLE app to provision a reachable AP <<<
```

The plan is printed once per retry loop (guarded by `wifi_reconnect_pending`). If no
credentials are stored, a warning and the provisioner tip are printed instead.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED | bool | n (enabled from prj.conf) | Enable BLE provisioning wrapper |

---

## API / Public Interface

- wifi_prov_over_ble_init() registered by SYS_INIT.
- Internal callbacks bridge provisioning events to logs and system settings.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| BLE stack init failure | return code on init | error log and disable provisioning flow |
| Provisioning failure | callback status | error log and keep device in provisioning state |
| Invalid credentials | connect failure after provision | disconnect path + user retries provisioning |
| Stored AP unavailable | connect timeout (`-ETIMEDOUT`) | credential rotation: `cred_rotate_idx` advances to next stored network; retry plan logged with schedule and provisioner tip |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | BLE + provisioning glue |
| RAM (static) | medium | BLE service runtime overhead |
| Stack | system workqueues | no dedicated app thread |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | provisioning module init log | BLE advertising visible |
| Successful provisioning | credential write success log | next connect succeeds without reprovision |
| Reboot persistence | reconnect logs on reboot | credentials retained in settings storage |
| Multi-AP retry (first AP unavailable) | `--- Retry schedule (N stored network(s)) ---` lines followed by per-SSID timing and provisioner tip | second stored SSID attempted after first times out; device connects on second retry |
