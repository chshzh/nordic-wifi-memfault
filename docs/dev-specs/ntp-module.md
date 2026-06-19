# NTP Sync Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-19-12-44 |
| PRD Version | 2026-06-19-12-31 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Implemented |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-04-23-33 | Formatted Document Information: `Module` → `Project`; added `NCS Version` and `Target Board(s)`. PRD Version updated to 2026-06-04-23-04. |
| 2026-05-14-15-00 | Initial spec for FR-006: NTP time synchronization |
| 2026-05-15-15-00 | Add downstream Memfault timestamp integration note |
| 2026-05-16-13-00 | Add CONFIG_NTP_RESYNC_INTERVAL_SEC — periodic re-sync after success; update state machine and Kconfig table |

---

## Overview

The NTP sync module queries an SNTP server after network connectivity is established
and sets the system real-time clock (CLOCK_REALTIME). When combined with
`CONFIG_LOG_TIMESTAMP_USE_REALTIME=y`, Zephyr log output shows ISO wall-clock
timestamps instead of uptime-relative milliseconds, making debug logs directly
correlatable with external events.

The module is Kconfig-gated (`CONFIG_NTP_MODULE`) and disabled by default so it
adds zero overhead to builds that do not need it.

**Downstream integration — Memfault event timestamps:** when `CONFIG_NTP_MODULE=y`,
the `app_memfault` module also compiles `memfault_platform_time.c`, which implements
`memfault_platform_time_get_current()` via `CLOCK_REALTIME`. This gives Memfault
events (log files, traces, heartbeats, crashes) accurate wall-clock timestamps on
the Memfault dashboard. Requires `CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM=y`
(set in `boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`). The function returns false
until epoch > 2020-01-01 to suppress epoch-0 dashboard noise before first sync.

---

## Location

- Path: `src/modules/ntp/`
- Files: `ntp_sync.c`, `ntp_sync.h`, `Kconfig.ntp`, `Kconfig.defaults`, `CMakeLists.txt`

---

## Module Type

- Application module (optional, Kconfig-gated)

---

## Zbus Integration

Subscribes (listener):
- `NETWORK_CHAN` (`struct network_msg`) — starts sync on `NETWORK_READY`, cancels
  pending retry work on `NETWORK_NOT_READY`

No channel publications.

---

## State Machine

```
IDLE
  │  NETWORK_READY received
  ▼
SYNCING  ──── sntp_simple() fails ──→  RETRY (k_work_delayable, CONFIG_NTP_RETRY_INTERVAL_SEC)
  │                                         │
  │  sntp_simple() succeeds                 │  NETWORK_NOT_READY received
  ▼                                         ▼
SYNCED ◄─── periodic re-sync ─────── SYNCED (k_work_delayable, CONFIG_NTP_RESYNC_INTERVAL_SEC)
  │
  │  NETWORK_NOT_READY received
  ▼
IDLE (synced flag cleared, work cancelled)
```

- On `NETWORK_READY`: if not yet synced, schedule work immediately.
- On successful sync: reschedule work after `CONFIG_NTP_RESYNC_INTERVAL_SEC` for drift compensation.
- On `NETWORK_NOT_READY`: cancel pending work (retry or re-sync), clear synced flag (re-sync on next connect).
- Retry and re-sync both use the same `k_work_delayable` item on the system work queue — no dedicated thread.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_NTP_MODULE` | bool | n | Enable NTP time synchronization module |
| `CONFIG_NTP_SERVER` | string | `"pool.ntp.org"` | SNTP server hostname |
| `CONFIG_NTP_TIMEOUT_MS` | int | 5000 | SNTP query timeout in ms (1000–30000) |
| `CONFIG_NTP_RETRY_INTERVAL_SEC` | int | 30 | Seconds between retries on failure (5–3600) |
|| `CONFIG_NTP_RESYNC_INTERVAL_SEC` | int | 10800 | Seconds between periodic re-syncs after success (60-86400); at 40 ppm, 21600 s (6 h) gives <=0.86 s drift |
| `CONFIG_NTP_MODULE_LOG_LEVEL` | choice | INF | Log verbosity for this module |

`CONFIG_NTP_MODULE` selects `CONFIG_SNTP` automatically.

---

## API / Public Interface

```c
/* ntp_sync.h */
int ntp_sync_init(void);   /* called by SYS_INIT — no user code needed */
```

Application code does not call this module directly. Time is available after sync via
standard POSIX `time()` / `clock_gettime(CLOCK_REALTIME, ...)`.

---

## Required prj.conf additions

```kconfig
CONFIG_NTP_MODULE=y
CONFIG_LOG_TIMESTAMP_USE_REALTIME=y
```

`CONFIG_SNTP` is selected automatically. No additional networking Kconfig is required —
DNS resolver and UDP sockets are already enabled by the network module.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| SNTP query timeout / network error | `sntp_simple()` returns `< 0` | Warning log + reschedule after `CONFIG_NTP_RETRY_INTERVAL_SEC` |
| `sys_clock_settime` failure | return code `< 0` | Error log; clock remains at uptime-relative value |
| Network drops before sync completes | `NETWORK_NOT_READY` event | Cancel work, reset synced flag; will re-sync on reconnect |

---

## Memory Estimate

| Resource | Estimate |
|----------|---------|
| Stack (work queue item) | Uses system work queue — no dedicated stack |
| Code flash | ~1 kB |
| RAM (state vars) | ~16 bytes |
