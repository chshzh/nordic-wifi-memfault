# NTP Module Specification

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
| 2026-07-24-14-09 | New spec — ported from `zego/bricks/ntp` (NCS v3.4.0 brick, spec version 2026-07-08-00-00), ported for FR-104. Unlike the brick's decoupled `ZEGO_NTP_NET_CHAN` + weak-hook publish pattern, this app already has `WIFI_CHAN`/`NETWORK_CHAN` zbus channels published by `network/net_event_mgmt.c`, so the module subscribes to `WIFI_CHAN` directly (same pattern as `app_memfault/core/memfault_core.c`'s `memfault_wifi_listener`) instead of introducing a second network-state channel. Kconfig prefix renamed `CONFIG_ZEGO_NTP_*` → `CONFIG_NTP_MODULE_*` to match this app's per-module naming convention (`CONFIG_WIFI_MODULE_*`, `CONFIG_HEAPS_MONITOR_*`). Two NCS v2.6.4-specific deviations from the brick: (1) `sys_clock_settime()`/`SYS_CLOCK_REALTIME` do not exist in this Zephyr version (3.5.99) — uses the POSIX `clock_settime(CLOCK_REALTIME, ...)` / `<zephyr/posix/time.h>` API instead, gated on `CONFIG_POSIX_CLOCK` (already enabled transitively by this app's Wi-Fi stack). (2) `CONFIG_LOG_TIMESTAMP_USE_REALTIME` does not exist in this Zephyr version either — UART log line timestamps remain device uptime; instead, this module implements `memfault_platform_time_get_current()` directly (this app has no `CONFIG_DATE_TIME`/`CONFIG_RTC`, so the Memfault Zephyr port's `MEMFAULT_SYSTEM_TIME_SOURCE` choice defaults to `..._CUSTOM`, which otherwise has no built-in implementation — every Memfault event/log was previously only ever timestamped at server ingest time). |
| 2026-07-24-14-41 | Two corrections found during hardware testing: (1) **Build fix** — `CMakeLists.txt` used the brick's `zephyr_library()`/`zephyr_library_sources()` pattern, which never links into this app's plain (non-west-module) `add_subdirectory()` build; `ntp_module_init()`'s `SYS_INIT` never ran and `ntp_wifi_listener` never subscribed, even though `CONFIG_NTP_MODULE_ENABLED=y` was correctly set. Switched to `target_sources(app PRIVATE ntp.c)`, this app's convention for every other module (confirmed fix via UART: `ntp_module: NTP sync initialized` / `Querying pool.ntp.org ...` now appear, and `ntp.c.obj` now compiles into `app.dir` instead of an unlinked separate library). Also added `ntp` to `main.c`'s boot-time "Enabled modules" list (previously missing). (2) **UART log timestamps** — `CONFIG_LOG_TIMESTAMP_USE_REALTIME` doesn't exist in this Zephyr version, but its logging core exposes `log_set_timestamp_func()` to swap the timestamp source used by ALL log lines at runtime. The module now registers a `CLOCK_REALTIME`-backed function once synced (freq=1, i.e. whole seconds — this project's log timestamps are 32-bit, so an epoch value in milliseconds would overflow); combined with `CONFIG_LOG_OUTPUT_FORMAT_LINUX_TIMESTAMP=y` (prj.conf), log lines switch from `[hh:mm:ss,ms,us]` uptime to `[<epoch_seconds>.000000]` Unix time once synced. |
| 2026-07-24-14-47 | Upgraded the log-timestamp rendering from raw epoch seconds to a calendar UTC string: registered a `log_custom_timestamp_format_func_t` via `log_custom_timestamp_set()` (`CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP=y`, replacing `CONFIG_LOG_OUTPUT_FORMAT_LINUX_TIMESTAMP`) that uses `gmtime_r()` to print `"2026-07-24 14:35:33Z"` once synced, or an elapsed `hh:mm:ss.mmm` duration (reconstructed manually, since custom-timestamp mode replaces Zephyr's built-in formatter entirely) before sync. |
| 2026-07-24-14-56 | **Bug fix**: hardware testing showed a handful of log lines briefly rendering bogus `1970-01-29` calendar dates (incrementing ~1 s per line) right around the sync transition. Root cause: the formatter checked the *live* `ntp_synced` flag at print time, but `CONFIG_LOG_MODE_DEFERRED` means messages are formatted asynchronously, sometime after they were captured — a message captured just before sync (uptime milliseconds) could still be sitting in the log buffer once `ntp_synced` flipped true, and got misinterpreted as epoch seconds. Fixed by tagging the epoch/uptime mode into bit 31 of the raw timestamp value itself at *capture* time (`NTP_LOG_TS_EPOCH_FLAG`), so formatting is correct regardless of processing delay. Also wrapped both formats in `"[...] "` to match Zephyr's original log style (`[00:06:43.892] ` / `[2026-07-24 14:35:33Z] `). |
| 2026-08-19-15-30 | Dropped nRF54LM20DK + nRF7002EB II from Target Board(s) — that board has no board definition in NCS v2.6.4 and has been removed project-wide; see `1-architecture.md` Changelog for the full removal. No module-specific behavior changed. |

---

## Overview

The NTP module synchronizes the system clock via SNTP once Wi-Fi connectivity is
established. It queries `CONFIG_NTP_MODULE_SERVER` and sets `CLOCK_REALTIME` via the
POSIX `clock_settime()` API (`sys_clock_settime()`/`SYS_CLOCK_REALTIME`, used by the
original `zego/bricks/ntp`, do not exist in this NCS v2.6.4 / Zephyr 3.5.99 tree).

Two things read that clock once set:

1. **UART log timestamps.** This Zephyr version has no `CONFIG_LOG_TIMESTAMP_USE_REALTIME`,
   but its logging core exposes `log_set_timestamp_func()` to swap the raw timestamp source at
   runtime, and `CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP` to swap how that raw value is
   printed. Once synced, this module registers a `CLOCK_REALTIME`-backed source (at 1 Hz —
   this project's log timestamps are 32-bit, so seconds is the largest unit that fits an epoch
   value without overflow) plus a custom formatter (`log_custom_timestamp_set()`) that renders
   it via `gmtime_r()` as a `"[2026-07-24 14:35:33Z] "` calendar UTC string, instead of the
   built-in `[hh:mm:ss,ms,us]` uptime format. Before sync, the same formatter falls back to
   rendering the raw value (still uptime milliseconds from Zephyr's default source) as an
   elapsed `"[hh:mm:ss.mmm] "` duration — required because enabling custom-timestamp mode
   replaces Zephyr's built-in formatter for *all* log lines, not just post-sync ones. Since
   `CONFIG_LOG_MODE_DEFERRED` formats messages asynchronously (sometime after they were
   captured), the epoch/uptime mode is tagged into bit 31 of the raw value itself at *capture*
   time (`NTP_LOG_TS_EPOCH_FLAG`) rather than decided from the live `ntp_synced` flag at print
   time — otherwise a message captured just before sync could still be in the log buffer once
   `ntp_synced` flips true, and get misrendered as a bogus 1970 date (found during hardware
   testing, see Changelog).
2. **Memfault.** This app has no `CONFIG_DATE_TIME`/`CONFIG_RTC`, so the Memfault Zephyr
   port's `MEMFAULT_SYSTEM_TIME_SOURCE` choice defaults to `MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM`,
   which has no built-in `memfault_platform_time_get_current()` — every Memfault event and log
   has so far only ever received a server ingest-time timestamp, never a device one. This module
   implements that missing function itself once it has synced, so Memfault events/logs —
   including the FR-102 disconnect-time log-state restore — get a real device UTC timestamp
   instead.

Failed queries are retried via a `k_work_delayable` item on the system work queue — no
dedicated thread required. A successful sync is periodically refreshed to compensate for
crystal oscillator drift. Disconnecting resets sync state so a fresh sync is performed after
each reconnect.

---

## Location

- **Path**: `src/modules/ntp/`
- **Files**: `ntp.c`, `ntp.h`, `Kconfig.ntp`, `Kconfig.defaults`, `CMakeLists.txt`

---

## Module Type

- [x] **Application module** — no SMF state machine, no dedicated thread; driven by a
  `k_work_delayable` item on the system work queue plus a `WIFI_CHAN` zbus listener.
- [ ] Library wrapper module (wraps Zephyr's `sntp_simple()` API, but this is a thin,
  app-owned integration rather than a distinct third-party library surface)

---

## Zbus Integration

**Subscribes to**: `WIFI_CHAN` (`ZBUS_LISTENER_DEFINE(ntp_wifi_listener, ...)`), the same
channel `network/net_event_mgmt.c` publishes `WIFI_STA_CONNECTED` / `WIFI_STA_DISCONNECTED`
to (see [network-module.md](network-module.md)). This module does not publish any zbus
channel of its own.

| Event received | Action |
|-----------------|--------|
| `WIFI_STA_CONNECTED` | If not already synced, schedule an immediate SNTP query (`K_NO_WAIT`) |
| `WIFI_STA_DISCONNECTED` | Cancel any pending/scheduled SNTP work, clear the synced flag |

---

## Memfault Integration

When `CONFIG_APP_MEMFAULT_MODULE=y` and `CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM` is
selected (the default in this app, since `CONFIG_DATE_TIME`/`CONFIG_RTC` are not enabled),
`ntp.c` implements `memfault_platform_time_get_current()`:

```c
bool memfault_platform_time_get_current(sMemfaultCurrentTime *time)
{
	struct timespec tspec;

	if (!ntp_synced) {
		return false;
	}
	if (clock_gettime(CLOCK_REALTIME, &tspec) != 0) {
		return false;
	}
	*time = (sMemfaultCurrentTime){
		.type = kMemfaultCurrentTimeType_UnixEpochTimeSec,
		.info.unix_timestamp_secs = (uint64_t)tspec.tv_sec,
	};
	return true;
}
```

Before this module existed, the Memfault SDK's weak default always returned `false` (no
time available), so every Memfault event/log was timestamped only at server ingest time.
If `CONFIG_DATE_TIME` or `CONFIG_RTC` is ever enabled in this project in the future, this
override will not compile (guarded by `#if ... CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM`)
and this module's `clock_settime()` call becomes redundant with that other time source.

---

## State Machine

Not applicable — no SMF; simple connected/disconnected + synced/unsynced state tracked with
two booleans (`ntp_network_ready`, `ntp_synced`), matching the ported brick's design.

---

## Behavior

1. On `WIFI_STA_CONNECTED`: if not already synced, schedule an immediate SNTP query.
2. On successful query: call `sys_clock_settime(SYS_CLOCK_REALTIME, ...)`, mark synced, and
   reschedule the next query after `CONFIG_NTP_MODULE_RESYNC_INTERVAL_SEC`.
3. On failed query: reschedule after `CONFIG_NTP_MODULE_RETRY_INTERVAL_SEC`.
4. On `WIFI_STA_DISCONNECTED`: cancel any pending work and clear the synced flag, so the
   next `WIFI_STA_CONNECTED` triggers an immediate re-sync.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|--------------|
| `CONFIG_NTP_MODULE_ENABLED` | bool | `y` | Enable the module; selects `CONFIG_SNTP`; depends on `ZBUS && NETWORKING` |
| `CONFIG_NTP_MODULE_SERVER` | string | `"pool.ntp.org"` | SNTP server hostname |
| `CONFIG_NTP_MODULE_TIMEOUT_MS` | int (1000–30000) | `5000` | Maximum time to wait for an SNTP response |
| `CONFIG_NTP_MODULE_RETRY_INTERVAL_SEC` | int (5–3600) | `30` | Retry interval after a failed query |
| `CONFIG_NTP_MODULE_RESYNC_INTERVAL_SEC` | int (60–86400, 0=disable) | `10800` | Periodic re-sync interval after a successful sync (3 h default keeps drift under ~0.5 s at 40 ppm) |
| `CONFIG_NTP_MODULE_LOG_LEVEL_*` | choice | `INF` | Log level (standard Zephyr log level template) |

---

## API / Public Interface

No public functions exported for other modules to call — this module is self-contained.

```c
/* Internal */
static void ntp_work_handler(struct k_work *work);
static void ntp_wifi_listener(const struct zbus_channel *chan);
```

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| SNTP query fails (`sntp_simple()` returns non-zero) | Return code check | `LOG_WRN`, reschedule after `CONFIG_NTP_MODULE_RETRY_INTERVAL_SEC` |
| `sys_clock_settime()` fails | Return code check | `LOG_ERR`, no retry scheduled for this cycle (next periodic/reconnect trigger will retry) |
| Disconnect while a query is in flight or pending | `WIFI_STA_DISCONNECTED` handler | `k_work_cancel_delayable()`; synced flag cleared so reconnect forces a fresh sync |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~1 KB | Single small file, no dedicated thread |
| RAM (static) | < 100 B | Two booleans + one `k_work_delayable` |
| Stack | Runs on system work queue | No dedicated thread |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|-----------------|
| Wi-Fi connects | `Querying pool.ntp.org ...` then `Time synced, epoch ...` | Within `CONFIG_NTP_MODULE_TIMEOUT_MS` of connect |
| Log timestamp switch | Log line prefix changes from `[00:00:29.357] ` (elapsed) to `[2026-07-24 14:35:33Z] ` (calendar UTC) | Immediately after `Time synced` line, no bogus 1970 dates on messages captured just before it |
| Disconnect then reconnect | A fresh `Querying ...` line shortly after `WIFI_STA_CONNECTED` fires again | Every reconnect |
| SNTP server unreachable | `SNTP query failed (...) - retry in Ns` | Retried every `CONFIG_NTP_MODULE_RETRY_INTERVAL_SEC` |
| Long uptime (> resync interval) | New `Querying ...` line every `CONFIG_NTP_MODULE_RESYNC_INTERVAL_SEC` | Periodic resync fires |
| FR-102 restore after NTP sync | Restored log-state entries and live Memfault events carry a real UTC timestamp instead of only a server ingest-time one | Only once NTP has completed its first sync |

---

## Open Issues / TBD

- [ ] Not yet included in a ZView-based memory measurement pass on either board (see [3-memopt.md](3-memopt.md) Open Issues).
- [ ] Flash/RAM estimate above is a rough placeholder pending hardware verification.

---

## Related Specs

- [1-architecture.md](1-architecture.md) — module map
- [network-module.md](network-module.md) — publishes `WIFI_STA_CONNECTED` / `WIFI_STA_DISCONNECTED` on `WIFI_CHAN`
- [app-memfault-module.md](app-memfault-module.md) — FR-102 restored log-state entries benefit from real UTC timestamps once this module has synced

*(Changelog is maintained at the top of this document.)*
