# App Memfault Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | app_memfault |
| Version | 2026-05-19-09-07 |
| PRD Version | 2026-05-19-09-07 |
| Author | GitHub Copilot |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/app_memfault implementation |
| 2026-05-15-10-31 | Add NETWORK_CHAN subscription; log-freeze-on-disconnect behaviour; sync success metrics |
| 2026-05-15-15-00 | Add NTP-backed Memfault timestamp provider; clarify on_connect() must NOT call log_trigger_collection(); add rate-limit constraint |
| 2026-05-15-22-02 | Button 1 short press: change to async upload via semaphore to avoid TLS-on-sysworkq stack overflow; add per-board coredump Kconfig; update upload thread scope |
| 2026-05-16-13-00 | OTA check interval changed to 30 min (48/day); document Memfault free-tier rate limits |
| 2026-05-16-17-00 | OTA check interval increased to 60 min (24/day) |
| 2026-05-19-09-07 | FR-007: connect-time ring-buffer restore; removed boot-time MEMFAULT_LOG_RESTORE_STATE hook (settings ordering issue); persist on disconnect, restore+upload on next WiFi connect |
| 2026-05-20-14-00 | Remove RAM-only freeze (trigger_collection on disconnect); persist-once guard (log_freeze_scheduled flag + k_work_schedule); trigger_collection moved to on_connect() after flash restore; visual log-restore boundary separator; WiFi CONNECT_RESULT failure triggers retry; both boards enabled for log-state restore |

---

## Overview

app_memfault is the integration boundary for Memfault cloud capabilities:
heartbeat/upload, crash capture, OTA checks, and optional nRF70 CDR uploads.
It consumes input events from BUTTON_CHAN, WIFI_CHAN, and NETWORK_CHAN and
translates them into Memfault SDK API operations.

**Log persist on connectivity loss:** whenever a connectivity loss is detected
(Wi-Fi deassociation via WIFI_CHAN, or IP-layer loss via NETWORK_CHAN), a
10-second delayable work item is scheduled — **once per disconnect event**.
A `log_freeze_scheduled` boolean guard ensures that only the first event in
a burst schedules the work; subsequent events (e.g. simultaneous
`WIFI_STA_DISCONNECTED` + `NETWORK_NOT_READY`) are silently ignored and do not
reset the timer. When the guard is set, `k_work_schedule()` (not
`k_work_reschedule`) is used so the timer fires exactly 10 s after the first
event. When the work fires, `log_freeze_scheduled` is cleared and
`memfault_log_state_persist_now()` saves the ring-buffer snapshot to external
flash. The guard is also cleared (and the pending work is cancelled) on WiFi
reconnect.

**Persist disconnect-time log state across reboot (FR-007, `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`):**
`memfault_log_state_persist_now()` serializes the entire Memfault ring-buffer
state (context struct + storage bytes) to a dedicated 8 KB partition on the
external SPI/QSPI NOR flash (`mflt-log-state` in mx25r64) immediately after
the 10 s disconnect delay fires. Enabled on both nRF54LM20DK and nRF7002DK.

On the next WiFi reconnect, `memfault_log_state_restore_on_connect()` is called at the
start of `on_connect()` — before any upload — while the flash driver is guaranteed
initialized (APPLICATION level threads are running). It loads the blob, validates
magic/version/size, then overwrites the live Memfault ring buffer in-place via
`memfault_log_get_state()` live pointers. After restore, `memfault_log_trigger_collection()`
is called to mark the restored ring-buffer content for upload (the blob saved to flash
contains no trigger watermark, so this step is required). The blob is erased from flash
immediately after restore. The subsequent `memfault_zephyr_port_post_data()` call uploads
the restored log file to Memfault, which shows original wall-clock timestamps (embedded
by the Zephyr log formatter at capture time). A visual separator log line
`=== [LOG RESTORE] pre-disconnect logs above | live session below ===`
is written to the ring buffer after restore so the boundary is visible in the
Memfault cloud log view.

**Why `MEMFAULT_LOG_RESTORE_STATE=1` but no `memfault_log_restore_state()` override?**
`MEMFAULT_LOG_RESTORE_STATE=1` is required to compile `memfault_log_get_state()`, which
the connect-time restore path uses to get live ring-buffer pointers. We intentionally do
NOT override the weak `memfault_log_restore_state()` callback: the SDK default returns
`false`, so the at-boot call (which fires before the external flash driver is ready) is a
harmless no-op. The actual restore is performed in `on_connect()` via
`memfault_log_state_restore_on_connect()`, where the flash driver is guaranteed initialized.

**`memfault_log_trigger_collection()` is called in `on_connect()` after restore, not on disconnect.**
The blob saved to external flash contains no trigger watermark (power-cycle removes it from
RAM). Calling `memfault_log_trigger_collection()` after restore marks the restored content
for upload. The NCS periodic upload (`CONFIG_MEMFAULT_PERIODIC_UPLOAD_INTERVAL_SECS`) calls
this API on its own schedule for normal (non-restore) cases. Calling it on disconnect would
freeze the RAM buffer even for brief, non-power-cycle losses where the ring buffer persists
across the reconnect — unnecessarily dropping new logs. By moving the trigger to after restore,
normal reconnects (no reboot) upload the full ring buffer as-is; only after a power cycle is
the trigger needed to mark the restored content.

**NTP-backed Memfault event timestamps (nrf54lm20dk only):** `memfault_platform_time.c`
implements `memfault_platform_time_get_current()` using `CLOCK_REALTIME`, which is set
by the NTP module after a successful SNTP sync. This gives Memfault events (crashes,
traces, metrics, log files) wall-clock timestamps on the dashboard instead of
epoch-0 placeholders. Enabled via `CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM=y`;
only compiled when `CONFIG_NTP_MODULE=y` (nrf54lm20dk board config).

---

## Location

- Path: src/modules/app_memfault/
- Files:
  - core/memfault_core.c(.h)
  - core/memfault_log_state_restore.c(.h) (when `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE=y`)
  - core/memfault_platform_time.c (nrf54lm20dk only — NTP-backed timestamp provider)
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
- `memfault_log_trigger_collection()` — mark restored ring-buffer for upload (**called in `on_connect()` after flash restore only** — never directly on disconnect)
- `memfault_platform_time_get_current()` — provide wall-clock timestamp from `CLOCK_REALTIME` (implemented in `memfault_platform_time.c`; returns false until NTP sync)
- `memfault_zephyr_port_post_data()` — post queued data; return code checked and logged. **Called only from the dedicated upload thread** (`memfault_upload_tid`), never from zbus listeners or the system workqueue.
- `memfault_metrics_heartbeat_debug_trigger()` — force heartbeat snapshot on connect
- `memfault_fota_start()` — initiate OTA check
- Memfault metrics/trace APIs used by metrics and button handlers

Callbacks and listener paths:
- WIFI_CHAN listener: `WIFI_STA_CONNECTED` clears `log_freeze_scheduled`, cancels pending persist work, and triggers DNS wait + upload; `WIFI_STA_DISCONNECTED` sets `log_freeze_scheduled` and schedules persist work (once).
- NETWORK_CHAN listener: `NETWORK_NOT_READY` sets `log_freeze_scheduled` and schedules persist work (once, only if not already scheduled by the WIFI listener).
- BUTTON_CHAN listener in core/ota/cdr maps button actions to heartbeat, OTA check,
  and crash demo triggers. **Button 1 short press** calls `memfault_metrics_heartbeat_debug_trigger()` then `k_sem_give(&upload_sem)` to wake the upload thread asynchronously — it does NOT call `memfault_zephyr_port_post_data()` directly, as that performs TLS operations unsuitable for the system workqueue stack.

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
| CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE | int | 5413 | Upload thread stack (sized for TLS; handles both on-connect and button-triggered uploads) |
| CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN | int | 60 | Periodic OTA check interval (24/day; free-tier limit 100/day) |
| CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC | int | 30 | Delay before OTA after event |
| CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE | int | 4096 | OTA trigger thread stack |
| CONFIG_NRF70_FW_STATS_CDR_ENABLED | bool | n (enabled in prj.conf) | Enable nRF70 CDR uploads |
| CONFIG_MEMFAULT_METRICS_SYNC_SUCCESS | bool | y | Enable sync_successful/sync_failure heartbeat counters |
| CONFIG_MEMFAULT_METRICS_MEMFAULT_SYNC_SUCCESS | bool | y | Enable sync_memfault_successful/failure counters for Memfault uploads |
| CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM | bool | y (nrf54lm20dk) | Use custom `memfault_platform_time_get_current()` backed by CLOCK_REALTIME (set by NTP module) |
| CONFIG_MEMFAULT_COREDUMP_STORAGE_RRAM | bool | y (nrf54lm20dk) | RRAM-backed coredump storage using DTS `memfault_coredump_partition` |
| CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM | bool | y (nrf7002dk) | Custom flash coredump backend in `core/memfault_flash_coredump_storage.c` using DTS `memfault_storage` partition; bypasses `PARTITION_MANAGER_ENABLED` dependency in upstream Kconfig |
| CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE | bool | y (nrf54lm20dk), n (nrf7002dk) | Persist Memfault ring-buffer state to settings on disconnect; restore and upload on next WiFi connect |
| CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE_MAX_BYTES | int | 3072 | Maximum settings payload bytes for the ring-buffer blob; bounded to protect the 8 KB shared settings partition |

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
| Wi-Fi deassociation | `WIFI_STA_DISCONNECTED` on WIFI_CHAN | connectivity metric → `ConnectionLost`; if not already scheduled, sets `log_freeze_scheduled` and schedules persist work |
| IP-layer loss (DHCP expiry, addr removal) | `NETWORK_NOT_READY` on NETWORK_CHAN | if not already scheduled, sets `log_freeze_scheduled` and schedules persist work; no connectivity metric change (Wi-Fi may still be associated) |
| Log-state persist failure | `flash_area_write()` returns error | `LOG_WRN`; runtime continues normally without crash |
| Log-state restore size mismatch | persisted `context_len`/`storage_len` differ from live ring-buffer sizes | discard blob, log warning; firmware update between boots is the typical cause |
| Log-state restore — no blob found | settings subtree empty or load error | silent no-op; normal upload proceeds |
| OTA check failure | `memfault_fota_start` result | log error and retain current firmware |
| CDR upload limit reached | Memfault CDR limit handling | skip until permitted window |
| Log file rate limit exceeded | Memfault backend silently drops assembled log file (device still receives HTTP 202 for chunks — **no firmware-side warning**) | visible only in Platform UI → Device → Developer Mode → Processing Errors; enable Server-Side Developer Mode for testing; set `MEMFAULT_PERIODIC_UPLOAD_INTERVAL_SECS=3600` for production |

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
| Wi-Fi disconnect | `WiFi DISCONNECTED` → `Network connectivity lost - scheduling Memfault log persist in N s` | single log line only (persist-once guard); `WIFI_STA_DISCONNECTED` without NETWORK_CHAN log would be a regression |
| IP-layer loss (DHCP) | `IP address removed from interface` → `Network connectivity lost - scheduling Memfault log persist in N s` (no duplicate if WIFI already scheduled) | NETWORK_CHAN listener acting independently of Wi-Fi layer |
| Upload failure (internet down) | `Memfault upload failed: <errno>` | error visible; `sync_memfault_failure` metric incremented in dashboard |
| Reconnect after disconnect (no reboot) | logs appear in Memfault Issues/Logs view | disconnect-time log snapshot preserved and uploaded |
| Reboot after disconnect → reconnect | `=== [LOG RESTORE] pre-disconnect logs above \| live session below ===` in Memfault cloud log; `Disconnect-time log state restored` on UART; Memfault Issues/Logs shows disconnect-time entries with original wall-clock timestamps | ring-buffer state survived power cycle via external flash; restore+upload succeeds on first connect after reboot |
| Reboot after disconnect → firmware update → reconnect | no restored logs (size mismatch discards blob); normal upload only | safe discard; no crash; expected after OTA |
| Memfault event timestamp (post NTP sync) | Memfault log file captured_date matches wall clock ± 2 s | NTP sync must precede the event; `CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM=y` required |
| Button 1 short | `Button 1 short press: Memfault heartbeat + upload` | heartbeat metric increment + upload triggered in upload thread |
| Button 2 short | OTA check trigger log | Memfault OTA check invoked |
| Button long press demos | crash trigger log | expected fault path for validation builds |
