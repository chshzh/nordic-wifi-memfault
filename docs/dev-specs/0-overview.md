# Engineering Specs Overview - nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-22-12-40 |
| PRD Version | 2026-06-19-12-31 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK |
| Status | Implemented |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-22-12-40 | app-memfault-module.md: LOGGING_RAM_SIZE 4096→8192; mflt-log-state 8→12 KB; add mflt_log_buf_bytes_used metric; remove stale LOG_STATE_RESTORE_MAX_BYTES Kconfig entry |
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-19-12-31 | Replaced heap-monitor-module.md with memonitor-module.md: deleted old spec, added memonitor-module.md (zego/memonitor brick), updated Spec Index, Zego table, PRD-to-Spec mapping, and dependency map. Architecture.md updated in parallel. |
| 2026-06-05-10-20 | Verification P1 fixes: removed stale Spec Index row for deleted app-wifi-prov-ble-module.md; corrected Section 6 SPECS_VERSION description (auto-extracted by zego/wifi CMakeLists.txt, not a manual define). |
| 2026-06-04-23-33 | Deleted button-module.md and app-wifi-prov-ble-module.md — both replaced by zego modules (`zego/button`, `zego/wifi_ble_prov`). Updated Spec Index, Zego table (added wifi_ble_prov; direct GitHub links), FR-001/FR-005 mappings, and dependency map. Version and PRD Version updated. |
| 2026-05-14-14-13 | Reverse-design baseline generated from current implementation in src/modules and migrated to docs/dev-specs |
| 2026-05-14-15-00 | Added ntp-module.md for FR-006: NTP time synchronization |
| 2026-05-19-09-07 | Added FR-007 design: connect-time ring-buffer restore for persist-across-reboot disconnect log upload |
| 2026-05-21-10-01 | Added FR-008 design: CDR flash persist/restore for disconnect-time nRF70 WiFi stats; new `mflt-cdr-state` external flash partition; `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` Kconfig |
| 2026-05-22-10-00 | Fixed multi-AP credential rotation in wifi_prov_over_ble: retry loop now cycles through all stored networks using `cred_rotate_idx`; retry schedule with provisioner reminder logged at retry loop start |
| 2026-06-04-23-00 | Added zego/led + LED UX module: `zego/led` registered as EXTRA_ZEPHYR_MODULE; `src/modules/ux/ux.c` drives LED 0 from `APP_WIFI_STATE_CHAN`; `net_event_app.c` now publishes `APP_WIFI_STATE_CHAN` alongside `NETWORK_CHAN`; button-module.md updated to zego redirect; new `ux.md` spec |

---

## 1. Purpose

This document is the entry point for engineering specs in nordic-wifi-memfault.
It is generated from implementation reality (reverse design) and is the canonical
spec set for the docs/ layout.

For product requirements driving this design, see [../pm-prd/PRD.md](../pm-prd/PRD.md).

---

## 2. Spec Index

### App-owned specs

| Spec file | Covers | PRD sections |
|-----------|--------|--------------|
| [architecture.md](architecture.md) | System architecture, module map, zbus channels, boot/init and thread budget | All |
| [network-module.md](network-module.md) | Wi-Fi/network event management, `WIFI_CHAN`, `NETWORK_CHAN`, `APP_WIFI_STATE_CHAN` publishing | FR-001, FR-002 |
| [ux.md](ux.md) | LED Wi-Fi state feedback (ROTATE/ON/BLINK via `zego/led`) | FR-009 |
| [memonitor-module.md](memonitor-module.md) | Heap + thread watermark telemetry, ZView live monitoring, Memfault metric feed | FR-002, NFR-001 |
| [app-memfault-module.md](app-memfault-module.md) | Memfault core, metrics, OTA triggers, CDR integration | FR-002, FR-003, FR-004 |
| [app-https-client-module.md](app-https-client-module.md) | HTTPS periodic health requests and metrics | FR-005 |
| [app-mqtt-client-module.md](app-mqtt-client-module.md) | MQTT echo client and connectivity metrics | FR-005 |
| [partition-layout.md](partition-layout.md) | Detailed PM-to-DTS partition migration, board layouts, OTA compatibility constraints | NFR-001 |
| [ntp-module.md](ntp-module.md) | NTP time synchronization — SNTP client, system clock set, log real-time timestamps | FR-006 |

### Zego library module references (no local src/)

| Module | Provided by | Canonical spec |
|--------|-------------|----------------|
| Button | `zego/modules/button` | [zego/button ↗](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) |
| LED | `zego/modules/led` | [zego/led ↗](https://github.com/chshzh/zego/blob/main/modules/led/docs/led-spec.md) |
| Wi-Fi BLE provisioning | `zego/modules/wifi_ble_prov` | [zego/wifi_ble_prov ↗](https://github.com/chshzh/zego/blob/main/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md) |
| Memory + thread monitor | `zego/bricks/memonitor` | [memonitor-module.md](memonitor-module.md) (local) |

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
| FR-001 Device connects to Wi-Fi with stored/provisioned credentials | network-module.md, [zego/wifi_ble_prov ↗](https://github.com/chshzh/zego/blob/main/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md) | Specified |
| FR-002 Upload Memfault data after connectivity ready | app-memfault-module.md, network-module.md, memonitor-module.md | Specified |
| FR-003 Button 1 behavior (heartbeat/CDR/stack-overflow demo) | [app-memfault-module.md](app-memfault-module.md), [zego/button ↗](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) | Specified |
| FR-004 Button 2 behavior (OTA check/div-by-zero demo) | [app-memfault-module.md](app-memfault-module.md), [zego/button ↗](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) | Specified |
| FR-005 BLE provisioning and optional HTTPS/MQTT clients | [zego/wifi_ble_prov ↗](https://github.com/chshzh/zego/blob/main/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md), app-https-client-module.md, app-mqtt-client-module.md | Specified |
| FR-006 NTP time sync for real-world log timestamps | ntp-module.md | Specified |
| FR-007 Persist disconnect-time log state across reboot; upload to Memfault on next connect | app-memfault-module.md, partition-layout.md | Specified |
| FR-008 CDR flash persist/restore for disconnect-time nRF70 WiFi stats | app-memfault-module.md, partition-layout.md | Specified |
| FR-009 LED Wi-Fi state feedback (nRF54LM20DK) | ux.md | Specified |
| NFR-001 Resource and stability constraints | architecture.md, memonitor-module.md | Specified |

---

## 5. Module Dependency Map

```
zego/button        (ext.) --> BUTTON_CHAN ------------> app_memfault (core/ota/cdr)
zego/led           (ext.) <-- LED_CMD_CHAN <----------- app_ux
zego/wifi_ble_prov (ext.) --> (credentials → NVS → network reconnect on next boot)
network --------------------> WIFI_CHAN --------------> app_memfault, app_https_client, app_mqtt_client
network --------------------> NETWORK_CHAN -----------> app_memfault (core)
network --------------------> APP_WIFI_STATE_CHAN -----> app_ux
zego/memonitor     (ext.) --> MEMONITOR_CHAN ---------> app_memfault (memonitor_metrics_listener)
```

For detailed channels and structs, see [architecture.md](architecture.md).

---

## 6. Conventions

**`SPECS_VERSION` — spec/code sync marker**

`SPECS_VERSION` is automatically extracted from this file's `| Version |` row by
`zego/modules/wifi/CMakeLists.txt` at build time and passed as a compile definition.
It is printed at boot by `zego_wifi_print_banner()` in `main.c`. No manual `#define`
is required.

After any spec or PRD change that affects behavior, update the `Version` field in
this file's Document Information table, append a changelog row, and rebuild — the
printed version will update automatically.

---

## 7. Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| 1 | Add docs/qa-test QA report snapshot for NCS v3.3.0 after hardware rerun | Team | Next QA cycle |
| 2 | Align README badge/version text from v3.2.4 to active v3.3.0 support matrix | Team | Next docs pass |
