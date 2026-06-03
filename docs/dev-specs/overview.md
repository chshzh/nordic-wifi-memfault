# Engineering Specs Overview - nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-05-22-10-00 |
| PRD Version | 2026-05-22-10-00 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design baseline generated from current implementation in src/modules and migrated to docs/dev-specs |
| 2026-05-14-15-00 | Added ntp-module.md for FR-006: NTP time synchronization |
| 2026-05-19-09-07 | Added FR-007 design: connect-time ring-buffer restore for persist-across-reboot disconnect log upload |
| 2026-05-21-10-01 | Added FR-008 design: CDR flash persist/restore for disconnect-time nRF70 WiFi stats; new `mflt-cdr-state` external flash partition; `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` Kconfig |
| 2026-05-22-10-00 | Fixed multi-AP credential rotation in wifi_prov_over_ble: retry loop now cycles through all stored networks using `cred_rotate_idx`; retry schedule with provisioner reminder logged at retry loop start |

---

## 1. Purpose

This document is the entry point for engineering specs in nordic-wifi-memfault.
It is generated from implementation reality (reverse design) and is the canonical
spec set for the docs/ layout.

For product requirements driving this design, see [../pm-prd/PRD.md](../pm-prd/PRD.md).

---

## 2. Spec Index

| Spec file | Covers | PRD sections |
|-----------|--------|--------------|
| [architecture.md](architecture.md) | System architecture, module map, zbus channels, boot/init and thread budget | All |
| [network-module.md](network-module.md) | Wi-Fi/network event management, WIFI_CHAN and NETWORK_CHAN publishing | FR-001, FR-002 |
| [app-wifi-prov-ble-module.md](app-wifi-prov-ble-module.md) | BLE Wi-Fi provisioning wrapper and credential flow | FR-005 |
| [heap-monitor-module.md](heap-monitor-module.md) | Heap telemetry and Memfault metric feed | FR-002, NFR-001 |
| [app-memfault-module.md](app-memfault-module.md) | Memfault core, metrics, OTA triggers, CDR integration | FR-002, FR-003, FR-004 |
| [app-https-client-module.md](app-https-client-module.md) | HTTPS periodic health requests and metrics | FR-005 |
| [app-mqtt-client-module.md](app-mqtt-client-module.md) | MQTT echo client and connectivity metrics | FR-005 |
| [partition-layout.md](partition-layout.md) | Detailed PM-to-DTS partition migration, board layouts, OTA compatibility constraints | NFR-001 |
| [ntp-module.md](ntp-module.md) | NTP time synchronization — SNTP client, system clock set, log real-time timestamps | FR-006 |

---

## 3. Architecture Summary

Pattern: Event-driven modular application using SYS_INIT + zbus listeners and dedicated
worker threads only where long-running network operations are required.

Key design decisions:

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Inter-module communication | zbus channels (BUTTON_CHAN/WIFI_CHAN/NETWORK_CHAN) | Loose coupling and simple feature expansion |
| Module startup | SYS_INIT in APPLICATION phase | Deterministic startup and early log visibility |
| Memfault integration | app_memfault wrapper module group | Isolates SDK integration from business logic |
| Optional features | Kconfig-gated modules (BLE/HTTPS/MQTT/CDR) | Build-time control by deployment profile |
| Security-sensitive values | Overlay file for project key | Prevents accidental secret commit |

---

## 4. PRD-to-Spec Mapping

| PRD requirement | Spec file | Status |
|----------------|-----------|--------|
| FR-001 Device connects to Wi-Fi with stored/provisioned credentials | network-module.md, app-wifi-prov-ble-module.md | Specified |
| FR-002 Upload Memfault data after connectivity ready | app-memfault-module.md, network-module.md, heap-monitor-module.md | Specified |
| FR-003 Button 1 behavior (heartbeat/CDR/stack-overflow demo) | [app-memfault-module.md](app-memfault-module.md) (button events via zego/button) | Specified |
| FR-004 Button 2 behavior (OTA check/div-by-zero demo) | [app-memfault-module.md](app-memfault-module.md) (button events via zego/button) | Specified |
| FR-005 BLE provisioning and optional HTTPS/MQTT clients | app-wifi-prov-ble-module.md, app-https-client-module.md, app-mqtt-client-module.md | Specified |
| FR-006 NTP time sync for real-world log timestamps | ntp-module.md | Specified |
| FR-007 Persist disconnect-time log state across reboot; upload to Memfault on next connect | app-memfault-module.md, partition-layout.md | Specified |
| NFR-001 Resource and stability constraints | architecture.md, heap-monitor-module.md | Specified |

---

## 5. Module Dependency Map

```
zego/button (external) ------> BUTTON_CHAN ------------------> app_memfault(core/ota/cdr)
network --------------------> WIFI_CHAN --------------------> app_memfault, wifi_prov_over_ble, app_https_client, app_mqtt_client
network --------------------> NETWORK_CHAN -----------------> reserved for future consumers
heap_monitor -----------------------------------------------> Memfault metrics (if app_memfault enabled)
```

For detailed channels and structs, see [architecture.md](architecture.md).

---

## 6. Conventions

**`SPECS_VERSION` — spec/code sync marker**

`src/main.c` defines:
```c
#define SPECS_VERSION "<latest overview.md changelog timestamp>"
```
and prints it at boot. After any spec or PRD change that affects behavior, update
`SPECS_VERSION` to match the new changelog timestamp and append a changelog row
to each affected spec and to PRD.md.

---

## 7. Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| 1 | Add docs/qa-test QA report snapshot for NCS v3.3.0 after hardware rerun | Team | Next QA cycle |
| 2 | Align README badge/version text from v3.2.4 to active v3.3.0 support matrix | Team | Next docs pass |
