# Heap Monitor Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-04-23-33 |
| PRD Version | 2026-06-04-23-04 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Implemented |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-23-33 | Formatted Document Information: `Module` → `Project`; added `NCS Version` and `Target Board(s)`. PRD Version updated to 2026-06-04-23-04. |
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/heap_monitor implementation |

---

## Overview

The heap monitor module periodically samples system heap and mbedTLS heap usage,
logs high-water conditions, and feeds heap-related Memfault metrics when the
Memfault module group is enabled.

---

## Location

- Path: src/modules/heap_monitor/
- Files: heap_monitor.c, Kconfig.heap_monitor, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Application module

---

## Zbus Integration

No direct zbus channel publishing/subscription in current implementation.
Metrics handoff is through Memfault metrics APIs.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_HEAPS_MONITOR | bool | n (enabled from prj.conf) | Enable heap monitor module |
| CONFIG_HEAPS_MONITOR_WARN_PCT | int | module default | Warn threshold percentage |
| CONFIG_HEAPS_MONITOR_STEP_BYTES | int | module default | Peak-step logging granularity |
| CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC | int | module default | Snapshot interval |

---

## API / Public Interface

- heap_monitor_init() via SYS_INIT.
- Internal periodic work performs sampling and reporting.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| Heap stats API unavailable | feature guard checks | skip unavailable source |
| Metrics write failure | return/error path | warn and continue |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | low | monitoring logic |
| RAM (static) | low | samples and counters |
| Stack | workqueue context | no dedicated thread |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | heap monitor enabled log | appears once at boot |
| Periodic sample | heap usage snapshot logs | interval matches config |
| High usage | warning threshold log | emitted when usage crosses threshold |
