# App Memfault Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | app_memfault |
| Version | 2026-05-14-14-13 |
| PRD Version | 2026-05-14-14-13 |
| Author | GitHub Copilot |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/app_memfault implementation |
| 2026-05-15-10-31 | Add NETWORK_CHAN subscription; log-freeze-on-disconnect behaviour; sync success metrics |

---

## Overview

app_memfault is the integration boundary for Memfault cloud capabilities:
heartbeat/upload, crash capture, OTA checks, and optional nRF70 CDR uploads.
It consumes input events from BUTTON_CHAN, WIFI_CHAN, and NETWORK_CHAN and
translates them into Memfault SDK API operations.

**Log freeze on connectivity loss:** whenever a connectivity loss is detected
(Wi-Fi deassociation via WIFI_CHAN, or IP-layer loss via NETWORK_CHAN),
`memfault_log_trigger_collection()` is called immediately. This freezes the
Memfault RAM log buffer at the disconnect-time snapshot — new logs are dropped
rather than overwriting the context. On the next successful upload the frozen
snapshot is sent to the Memfault backend and the buffer is unfrozen.

---

## Location

- Path: src/modules/app_memfault/
- Files:
  - core/memfault_core.c(.h)
  - metrics/wifi_metrics.c(.h), metrics/stack_metrics.c(.h)
  - ota/ota_triggers.c(.h)
  - cdr/nrf70_fw_stats_cdr.c(.h)
  - config/memfault_metrics_heartbeat_config.def
  - Kconfig.app_memfault, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Library wrapper module

---

## External Library Interface

| Field | Value |
|-------|-------|
| Library | Memfault SDK (NCS integration) |
| NCS Kconfig | CONFIG_MEMFAULT, CONFIG_MEMFAULT_FOTA, related symbols selected by APP_MEMFAULT_MODULE |
| Library internal threads | Upload/FOTA workqueues managed by Memfault/NCS ports |

Representative API usage from this wrapper module:
- `memfault_log_trigger_collection()` — freeze log buffer on connectivity loss
- `memfault_zephyr_port_post_data()` — post queued data; return code checked and logged
- `memfault_metrics_heartbeat_debug_trigger()` — force heartbeat snapshot on connect
- `memfault_fota_start()` — initiate OTA check
- Memfault metrics/trace APIs used by metrics and button handlers

Callbacks and listener paths:
- WIFI_CHAN listener: `WIFI_STA_CONNECTED` triggers DNS wait + upload; `WIFI_STA_DISCONNECTED`
  updates connectivity metrics and calls `memfault_log_trigger_collection()`.
- NETWORK_CHAN listener: `NETWORK_NOT_READY` calls `memfault_log_trigger_collection()`
  to catch IP-layer losses (DHCP expiry, address removal) independently of Wi-Fi association.
- BUTTON_CHAN listener in core/ota/cdr maps button actions to heartbeat, OTA check,
  and crash demo triggers.

---

## Zbus Integration

Subscribes to:
- WIFI_CHAN (connect/disconnect flow)
- NETWORK_CHAN (IP-layer connectivity loss — DHCP expiry, address removal)
- BUTTON_CHAN (user action flow)

Does not define its own public zbus channel in current implementation.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_APP_MEMFAULT_MODULE | bool | n (enabled from prj.conf) | Enable module group |
| CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE | int | 4096 | On-connect upload thread stack |
| CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN | int | 60 | Periodic OTA check interval |
| CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC | int | 30 | Delay before OTA after event |
| CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE | int | 4096 | OTA trigger thread stack |
| CONFIG_NRF70_FW_STATS_CDR_ENABLED | bool | n (enabled in prj.conf) | Enable nRF70 CDR uploads |
| CONFIG_MEMFAULT_METRICS_SYNC_SUCCESS | bool | y | Enable sync_successful/sync_failure heartbeat counters |
| CONFIG_MEMFAULT_METRICS_MEMFAULT_SYNC_SUCCESS | bool | y | Enable sync_memfault_successful/failure counters for Memfault uploads |

---

## API / Public Interface

This wrapper is primarily event-driven via SYS_INIT and zbus listeners.
Public headers expose module-specific helpers used within module group components.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| DNS not ready on connect | `getaddrinfo` polling with 10 s interval, 300 s total timeout | warn log; continue anyway after timeout |
| Upload failure (Wi-Fi up, internet down) | `memfault_zephyr_port_post_data()` returns non-zero | `LOG_ERR` with error code; `sync_memfault_failure` metric incremented; periodic timer retries |
| Wi-Fi deassociation | `WIFI_STA_DISCONNECTED` on WIFI_CHAN | connectivity metric → `ConnectionLost`; `memfault_log_trigger_collection()` freezes log buffer |
| IP-layer loss (DHCP expiry, addr removal) | `NETWORK_NOT_READY` on NETWORK_CHAN | `memfault_log_trigger_collection()` freezes log buffer; no connectivity metric change (Wi-Fi may still be associated) |
| OTA check failure | `memfault_fota_start` result | log error and retain current firmware |
| CDR upload limit reached | Memfault CDR limit handling | skip until permitted window |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | high | Memfault integration + optional CDR/OTA paths |
| RAM (static) | medium | states, buffers, metrics context |
| Stack | dedicated upload/OTA threads | sizes set by project tuning in prj.conf |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | `Memfault core init` | module active |
| Wi-Fi connect | `Connected to network` → `Sending already captured data to Memfault` | data reaches Memfault backend |
| Wi-Fi disconnect | `WiFi DISCONNECTED` → `Network connectivity lost — freezing Memfault logs` | log buffer frozen; `WIFI_STA_DISCONNECTED` without NETWORK_CHAN log would be a regression |
| IP-layer loss (DHCP) | `IP address removed from interface` → `Network connectivity lost — freezing Memfault logs` (no `WiFi DISCONNECTED` line) | NETWORK_CHAN listener acting independently of Wi-Fi layer |
| Upload failure (internet down) | `Memfault upload failed: <errno>` | error visible; `sync_memfault_failure` metric incremented in dashboard |
| Reconnect after disconnect | frozen logs appear in Memfault Issues/Logs view | disconnect-time log snapshot preserved and uploaded |
| Button 1 short | heartbeat trigger log | heartbeat metric increment |
| Button 2 short | OTA check trigger log | Memfault OTA check invoked |
| Button long press demos | crash trigger log | expected fault path for validation builds |
