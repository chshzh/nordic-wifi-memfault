# Heap Monitor Module Specification

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
| 2026-07-13-11-08 | New spec — module did not exist at the time of the legacy `pm/openspec` docs. Added since the pre-migration snapshot to track system-heap and mbedTLS-heap usage and feed it into Memfault metrics. |
| 2026-08-19-15-30 | Dropped nRF54LM20DK + nRF7002EB II from Target Board(s) — that board has no board definition in NCS v2.6.4 and has been removed project-wide; see `1-architecture.md` Changelog for the full removal. No module-specific behavior changed. |

---

## Overview

The Heap Monitor module periodically (and on significant peak increases) logs system-heap
and, if enabled, mbedTLS-heap utilization, and reports the same values as Memfault metrics
when `app_memfault` is enabled. It is a passive observer — it does not manage or resize any
heap, only measures and reports.

---

## Location

- **Path**: `src/modules/heap_monitor/`
- **Files**: `heap_monitor.c`, `Kconfig.heap_monitor`, `Kconfig.defaults`, `CMakeLists.txt`

---

## Module Type

- [x] **Application module** — standalone monitor, no SMF state machine, no dedicated thread beyond a periodic work item.
- [ ] Library wrapper module (it *reads* system/mbedTLS heap internals but does not wrap an external library API surface in the sense of calling/implementing a library's public interface)

---

## Zbus Integration

This module does not use Zbus. It is driven by a periodic timer/work item
(`CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC`) and by heap-listener callbacks
(`CONFIG_SYS_HEAP_LISTENER`) for peak-triggered reports.

---

## State Machine

Not applicable — no SMF, simple periodic + threshold-triggered reporting.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|--------------|
| `CONFIG_HEAPS_MONITOR` | bool | (depends on `HEAP_MEM_POOL_SIZE > 0` or `MBEDTLS_ENABLE_HEAP`) | Enable the module; selects `SYS_HEAP_LISTENER` + `SYS_HEAP_RUNTIME_STATS` (if system heap in use) and `MBEDTLS_MEMORY_DEBUG` (if mbedTLS heap in use) |
| `CONFIG_HEAPS_MONITOR_WARN_PCT` | int (50–100) | project-set in `prj.conf`/defaults | Emit `LOG_WRN` when used% reaches this threshold |
| `CONFIG_HEAPS_MONITOR_STEP_BYTES` | int (128–8192) | project-set | Minimum peak increase (bytes) before logging a new all-time-high report |
| `CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC` | int (1–86400) | `120` (`prj.conf`) | Interval between standardized heap snapshot logs |
| `CONFIG_HEAPS_MONITOR_LOG_LEVEL_*` | choice | `INF` | Log level (`prj.conf`) |

---

## API / Public Interface

No public functions exported for other modules to call — this module is self-contained and
only produces log output + Memfault metric updates.

```c
/* Internal helpers (not exported) */
static void update_system_heap_metrics(uint32_t total, uint32_t used, uint32_t peak);
static int get_system_heap_stats(struct sys_memory_stats *stats);
static void report_system_heap_peak_if_needed(void);
```

Memfault metrics updated (when `CONFIG_APP_MEMFAULT_MODULE=y`):
`ncs_system_heap_total`, `ncs_system_heap_used`, `ncs_system_heap_peak` (and equivalent
mbedTLS-heap metrics when `CONFIG_MBEDTLS_ENABLE_HEAP=y` — see `heap_monitor.c` for the
mbedTLS-specific reporting path).

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| `sys_heap_runtime_stats_get()` fails | Non-zero return in `get_system_heap_stats` | Skip this sample; no crash, no log spam |
| `total == 0` (heap not yet initialized) | Checked in `report_system_heap_peak_if_needed` | Skip this sample |
| Heap usage exceeds `CONFIG_HEAPS_MONITOR_WARN_PCT` | Percentage check | `LOG_WRN` instead of `LOG_INF`; when `CONFIG_APP_MEMFAULT_MODULE` is disabled, `MEMFAULT_METRIC_SET_UNSIGNED` becomes a no-op macro (`((void)0)`) |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~2 KB | Small, single-file module — not yet independently measured |
| RAM (static) | ~1 KB | A handful of static counters (`system_last_reported_high`, `system_last_warn_pct`, mbedTLS peak trackers) |
| Stack | Runs on system work queue / heap-listener callback context | No dedicated thread |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Periodic snapshot | `System Heap: used=.../... (...%) blocks=..., peak=.../... (...%), peak_blocks=...` | Every `CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC` |
| Peak increase report | Same log line at `LOG_WRN` if over threshold | Peak increased by ≥ `CONFIG_HEAPS_MONITOR_STEP_BYTES` since last report |
| Warning threshold crossed | `LOG_WRN` line | used%/peak% ≥ `CONFIG_HEAPS_MONITOR_WARN_PCT` |

---

## Open Issues / TBD

- [ ] Not yet included in a ZView-based memory measurement pass on either board (see [3-memopt.md](3-memopt.md) Open Issues).
- [ ] Flash/RAM estimate above is a rough placeholder, not a measured value.

---

## Related Specs

- [1-architecture.md](1-architecture.md) — module map
- [app-memfault-module.md](app-memfault-module.md) — consumes heap metrics via `MEMFAULT_METRIC_SET_UNSIGNED`

*(Changelog is maintained at the top of this document.)*
