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

---

## Overview

app_memfault is the integration boundary for Memfault cloud capabilities:
heartbeat/upload, crash capture, OTA checks, and optional nRF70 CDR uploads.
It consumes input events from BUTTON_CHAN and WIFI_CHAN and translates them
into Memfault SDK API operations.

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
- memfault_zephyr_port_post_data()
- memfault_metrics_heartbeat_debug_trigger()
- memfault_fota_start()
- Memfault metrics/trace APIs used by metrics and button handlers

Callbacks and listener paths:
- WIFI_CHAN listener in core triggers DNS wait and upload path on connect events.
- BUTTON_CHAN listener in core/ota/cdr maps button actions to heartbeat, OTA check,
  and crash demo triggers.

---

## Zbus Integration

Subscribes to:
- WIFI_CHAN (connect/disconnect flow)
- BUTTON_CHAN (user action flow)

Does not define its own public zbus channel in current implementation.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_APP_MEMFAULT_MODULE | bool | n (enabled from prj.conf) | Enable module group |
| CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE | int | module default/overridden in prj.conf | On-connect upload thread stack |
| CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN | int | module default/overridden | Periodic OTA check interval |
| CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC | int | module default/overridden | Delay before OTA after event |
| CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE | int | module default/overridden | OTA trigger thread stack |
| CONFIG_NRF70_FW_STATS_CDR_ENABLED | bool | n (enabled in prj.conf) | Enable nRF70 CDR uploads |

---

## API / Public Interface

This wrapper is primarily event-driven via SYS_INIT and zbus listeners.
Public headers expose module-specific helpers used within module group components.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| DNS not ready on connect | getaddrinfo polling timeout path | warn log; retry on future connectivity/events |
| Upload failure | return codes/log errors | log and continue periodic/event retries |
| OTA check failure | memfault_fota_start result | log error and retain current firmware |
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
| Init | app_memfault init logs | module active |
| Wi-Fi connect | DNS wait/upload logs | data reaches Memfault backend |
| Button 1 short | heartbeat trigger log | heartbeat metric increment and optional CDR path |
| Button 2 short | OTA check trigger log | Memfault OTA check invoked |
| Button long press demos | crash trigger log | expected fault path for validation builds |
