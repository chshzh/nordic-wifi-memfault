# NTP Sync Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-07-08-00-02 |
| PRD Version | 2026-07-07-16-32 |
| NCS Version | v3.4.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Moved to `zego/bricks/ntp` |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-08-00-02 | **Moved from `src/modules/ntp/` to `zego/bricks/ntp/`** as a first-class zego brick, and adopted by the `zego/nordic-wifi-app-template` sample. Kconfig prefix `CONFIG_NTP_MODULE`/`CONFIG_NTP_*` → `CONFIG_ZEGO_NTP`/`CONFIG_ZEGO_NTP_*`. The module no longer subscribes to this app's local `NETWORK_CHAN`; it now declares its own `ZEGO_NTP_NET_CHAN` (`struct zego_ntp_net_msg { bool connected; }`), published by `src/modules/network/net_event_mgmt.c` (this app's `zego/network` weak-hook overrides) alongside the existing `NETWORK_CHAN` / `WIFI_CHAN` / `ZEGO_UX_WIFI_STATE_CHAN` publishes. Public API renamed `ntp_sync_init()` → `zego_ntp_init()`. See [zego/bricks/ntp/docs/ntp-spec.md](../../../zego/bricks/ntp/docs/ntp-spec.md) for the canonical spec; this page is kept only for app-level integration notes (thread stack metric name, board Kconfig). |
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-04-23-33 | Formatted Document Information: `Module` → `Project`; added `NCS Version` and `Target Board(s)`. PRD Version updated to 2026-06-04-23-04. |
| 2026-05-14-15-00 | Initial spec for FR-006: NTP time synchronization |
| 2026-05-15-15-00 | Add downstream Memfault timestamp integration note |
| 2026-05-16-13-00 | Add CONFIG_NTP_RESYNC_INTERVAL_SEC — periodic re-sync after success; update state machine and Kconfig table |

---

## Overview

NTP time synchronization is now provided by the `zego/ntp` brick (see
[zego/bricks/ntp/docs/ntp-spec.md](../../../zego/bricks/ntp/docs/ntp-spec.md) for
the full API, Kconfig reference, state machine, and test points). This page only
documents how `nordic-wifi-memfault` wires the brick in.

The brick queries an SNTP server once network connectivity is established and
sets the system real-time clock (`CLOCK_REALTIME`). When combined with
`CONFIG_LOG_TIMESTAMP_USE_REALTIME=y`, Zephyr log output shows ISO wall-clock
timestamps instead of uptime-relative milliseconds, making debug logs directly
correlatable with external events.

The brick is Kconfig-gated (`CONFIG_ZEGO_NTP`) and disabled by default so it
adds zero overhead to builds that do not need it.

**Downstream integration — Memfault event timestamps:** when `CONFIG_ZEGO_NTP=y`,
the `app_memfault` module also compiles `app_memfault_platform_time.c`, which
implements `memfault_platform_time_get_current()` via `CLOCK_REALTIME`. This gives
Memfault events (log files, traces, heartbeats, crashes) accurate wall-clock
timestamps on the Memfault dashboard. Requires
`CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM=y` (set in `prj.conf`). The function
returns false until epoch > 2020-01-01 to suppress epoch-0 dashboard noise before
first sync.

---

## Location

- Brick path: `zego/bricks/ntp/` — `src/ntp.c`, `src/ntp.h`, `Kconfig`,
  `CMakeLists.txt`, `zephyr/module.yml`, `docs/ntp-spec.md`
- App integration: `src/modules/network/net_event_mgmt.c` publishes
  `ZEGO_NTP_NET_CHAN`; `src/modules/app_memfault/` consumes `CONFIG_ZEGO_NTP` to
  gate `core/app_memfault_platform_time.c` and the `ncs_ntp_unused_stack` metric.

---

## Module Type

- External zego brick, registered via `EXTRA_ZEPHYR_MODULES` in `CMakeLists.txt`
  (optional, Kconfig-gated).

---

## Zbus Integration

Subscribes (listener, inside the brick):
- `ZEGO_NTP_NET_CHAN` (`struct zego_ntp_net_msg`) — starts sync on
  `connected = true`, cancels pending retry work on `connected = false`.

Published by (this app):
- `src/modules/network/net_event_mgmt.c`'s `zego_on_net_event_dhcp_bound()` /
  `zego_on_net_event_wifi_disconnect()` — the same weak-hook overrides that
  already publish `NETWORK_CHAN`, `WIFI_CHAN`, and `ZEGO_UX_WIFI_STATE_CHAN`.

No channel publications from the brick itself.

---

## State Machine

```
IDLE
  │  connected = true received on ZEGO_NTP_NET_CHAN
  ▼
SYNCING  ──── sntp_simple() fails ──→  RETRY (k_work_delayable, CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC)
  │                                         │
  │  sntp_simple() succeeds                 │  connected = false received
  ▼                                         ▼
SYNCED ◄─── periodic re-sync ─────── SYNCED (k_work_delayable, CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC)
  │
  │  connected = false received
  ▼
IDLE (synced flag cleared, work cancelled)
```

- On `connected = true`: if not yet synced, schedule work immediately.
- On successful sync: reschedule work after `CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC` for drift compensation.
- On `connected = false`: cancel pending work (retry or re-sync), clear synced flag (re-sync on next connect).
- Retry and re-sync both use the same `k_work_delayable` item on the system work queue — no dedicated thread.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_ZEGO_NTP` | bool | n | Enable the zego/ntp brick |
| `CONFIG_ZEGO_NTP_SERVER` | string | `"pool.ntp.org"` | SNTP server hostname |
| `CONFIG_ZEGO_NTP_TIMEOUT_MS` | int | 5000 | SNTP query timeout in ms (1000–30000) |
| `CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC` | int | 30 | Seconds between retries on failure (5–3600) |
| `CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC` | int | 10800 | Seconds between periodic re-syncs after success (60-86400); at 40 ppm, 21600 s (6 h) gives <=0.86 s drift |
| `CONFIG_ZEGO_NTP_LOG_LEVEL` | choice | INF | Log verbosity for this brick |

`CONFIG_ZEGO_NTP` selects `CONFIG_SNTP` automatically.

---

## API / Public Interface

```c
/* zego/bricks/ntp/src/ntp.h */
int zego_ntp_init(void);   /* called by SYS_INIT — no user code needed */
```

Application code does not call this brick directly. Time is available after sync via
standard POSIX `time()` / `clock_gettime(CLOCK_REALTIME, ...)`.

---

## Required prj.conf additions

```kconfig
CONFIG_ZEGO_NTP=y
CONFIG_LOG_TIMESTAMP_USE_REALTIME=y
```

`CONFIG_SNTP` is selected automatically. No additional networking Kconfig is required —
DNS resolver and UDP sockets are already enabled by the network module.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| SNTP query timeout / network error | `sntp_simple()` returns `< 0` | Warning log + reschedule after `CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC` |
| `sys_clock_settime` failure | return code `< 0` | Error log; clock remains at uptime-relative value |
| Network drops before sync completes | `connected = false` on `ZEGO_NTP_NET_CHAN` | Cancel work, reset synced flag; will re-sync on reconnect |

---

## Memory Estimate

| Resource | Estimate |
|----------|---------|
| Stack (work queue item) | Uses system work queue — no dedicated stack |
| Code flash | ~1 kB |
| RAM (state vars) | ~16 bytes |

