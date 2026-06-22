# App Memfault Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-22-12-40 |
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
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/app_memfault implementation |
| 2026-05-15-10-31 | Add NETWORK_CHAN subscription; log-freeze-on-disconnect behaviour; sync success metrics |
| 2026-05-15-15-00 | Add NTP-backed Memfault timestamp provider; clarify on_connect() must NOT call log_trigger_collection(); add rate-limit constraint |
| 2026-05-15-22-02 | Button 1 short press: change to async upload via semaphore to avoid TLS-on-sysworkq stack overflow; add per-board coredump Kconfig; update upload thread scope |
| 2026-05-16-13-00 | OTA check interval changed to 30 min (48/day); document Memfault free-tier rate limits |
| 2026-05-16-17-00 | OTA check interval increased to 60 min (24/day) |
| 2026-05-19-09-07 | FR-007: connect-time ring-buffer restore; removed boot-time MEMFAULT_LOG_RESTORE_STATE hook (settings ordering issue); persist on disconnect, restore+upload on next WiFi connect |
| 2026-05-20-14-00 | Remove RAM-only freeze (trigger_collection on disconnect); persist-once guard (log_freeze_scheduled flag + k_work_schedule); trigger_collection moved to on_connect() after flash restore; visual log-restore boundary separator; WiFi CONNECT_RESULT failure triggers retry; both boards enabled for log-state restore |
| 2026-05-21-10-01 | FR-008: CDR flash persist on disconnect; CDR blob restore on next WiFi connect; new `mflt_cdr_state_partition` external flash partition (8 KB); `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` Kconfig; persist/restore functions added to nrf70_fw_stats_cdr.c; memfault_core.c integrated in log_freeze_work_fn() and on_connect() |
| 2026-05-26-15-21 | Add Metrics Reference section: metric types, naming conventions, runtime API, and category table; add disconnect reason layer note cross-referenced from network-module.md |

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
external SPI/QSPI NOR flash (`mflt-log-state` in mx25r64, **12 KB**) immediately after
the 10 s disconnect delay fires. Enabled on both nRF54LM20DK and nRF7002DK.

On the next WiFi reconnect, `memfault_log_state_restore_on_connect()` is called at the
start of `on_connect()` — before any upload — while the flash driver is guaranteed
initialized (APPLICATION level threads are running). It loads the blob, validates
magic/version/size, then overwrites the live Memfault ring buffer in-place via
`memfault_log_get_state()` live pointers. After restore, a `LOG_INF` emits `"Mflt log trigger (restore): N unsent logs, X/8192 bytes used"` and
`MEMFAULT_METRIC_SET_UNSIGNED(mflt_log_buf_bytes_used, X)` records the fill level for the heartbeat.
`memfault_log_trigger_collection()` is then called to mark the restored content for upload. The blob is erased from flash
immediately after restore. The subsequent `memfault_zephyr_port_post_data()` call uploads
the restored log file to Memfault, which shows original wall-clock timestamps (embedded
by the Zephyr log formatter at capture time). A visual separator log line
`=== [LOG RESTORE] pre-disconnect logs above | live session below ===`
is written to the ring buffer after restore so the boundary is visible in the
Memfault cloud log view.

**CDR persist on connectivity loss (FR-008, `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`):**
When the `log_freeze_work` fires (10 s after connectivity loss), `mflt_nrf70_fw_stats_cdr_persist_to_flash()`
is called immediately after the log-state persist. It collects a fresh snapshot of nRF70 firmware
statistics (PHY/LMAC/UMAC) via the same FMAC API used by the button-triggered path, then writes a
16-byte header (`CDRS` magic, version 1, blob size) followed by the raw blob to the dedicated 8 KB
`mflt-cdr-state` partition on the external SPI/QSPI NOR flash. Payload-then-header write ordering
ensures that a partial write leaves the partition without a valid header, so restore safely returns
`-ENOENT` on next boot. If the nRF70 driver is unavailable at collect time (e.g. device is being
reset), the function returns `-ENODEV` and a warning is logged; no flash write is performed.

**CDR restore and upload on next connect (FR-008, `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`):**
In `on_connect()`, after log-state restore, `mflt_nrf70_fw_stats_cdr_restore_from_flash()` reads the
header and blob from the `mflt-cdr-state` partition. On a valid header, it copies the blob into the
CDR module's internal `s_nrf70_fw_stats_blob` buffer, sets `s_nrf70_fw_stats_blob_size`, and marks
`s_cdr_data_ready = true`. The partition is erased immediately after restore (one-shot). The CDR
source is already registered at APPLICATION init (`mflt_nrf70_fw_stats_cdr_init()`), so `has_cdr_cb`
returns `true` on the next `memfault_packetizer_data_available()` poll, and the subsequent
`memfault_zephyr_port_post_data()` call uploads the CDR file. `mark_cdr_read_cb()` then clears the
ready flag. If the stored blob size exceeds `NRF70_FW_STATS_BLOB_MAX_SIZE`, it is discarded with a
warning. Enabled on both boards (nRF54LM20DK and nRF7002DK).


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
  - cdr/nrf70_fw_stats_cdr.c(.h) (includes flash persist/restore when `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE=y`)
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
| CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE | int | 6160 | Upload thread stack (sized for TLS; handles both on-connect and button-triggered uploads) |
| CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN | int | 60 | Periodic OTA check interval (24/day; free-tier limit 100/day) |
| CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC | int | 30 | Delay before OTA after event |
| CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE | int | 6330 | OTA trigger thread stack |
| CONFIG_NRF70_FW_STATS_CDR_ENABLED | bool | n (enabled in prj.conf) | Enable nRF70 CDR uploads |
| CONFIG_MEMFAULT_METRICS_SYNC_SUCCESS | bool | y | Enable sync_successful/sync_failure heartbeat counters |
| CONFIG_MEMFAULT_METRICS_MEMFAULT_SYNC_SUCCESS | bool | y | Enable sync_memfault_successful/failure counters for Memfault uploads |
| CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM | bool | y (nrf54lm20dk) | Use custom `memfault_platform_time_get_current()` backed by CLOCK_REALTIME (set by NTP module) |
| CONFIG_MEMFAULT_COREDUMP_STORAGE_RRAM | bool | y (nrf54lm20dk) | RRAM-backed coredump storage using DTS `memfault_coredump_partition` |
| CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM | bool | y (nrf7002dk) | Custom flash coredump backend in `core/memfault_flash_coredump_storage.c` using DTS `memfault_storage` partition; bypasses `PARTITION_MANAGER_ENABLED` dependency in upstream Kconfig |
| CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE | bool | y (both boards) | Persist Memfault ring-buffer state to dedicated external flash partition on disconnect; restore and upload on next WiFi connect |
| CONFIG_MEMFAULT_LOGGING_RAM_SIZE | int | 8192 | Memfault log ring-buffer size (RAM); flash partition `mflt-log-state` sized to 12 KB to hold the full blob (16 B hdr + context + 8192 B storage) |
| CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE | bool | y (both boards) | Persist disconnect-time nRF70 CDR (WiFi fw stats) blob to external flash; restore and upload on next WiFi connect. Depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED`. |

---

## Metrics Reference

### Heartbeat metric definition file

Custom metrics are declared in `config/memfault_metrics_heartbeat_config.def`. Each entry
calls one of two macros:

```c
/* Always-present metric */
MEMFAULT_METRICS_KEY_DEFINE(key_name, type)

/* Conditionally compiled metric */
#if CONFIG_SOME_MODULE
MEMFAULT_METRICS_KEY_DEFINE(app_my_counter, kMemfaultMetricType_Unsigned)
#endif
```

### Metric types

| Type constant | Use case |
|---------------|----------|
| `kMemfaultMetricType_Unsigned` | Monotonic counters, sizes, stack watermarks |
| `kMemfaultMetricType_Signed` | Values that may be negative (e.g. RSSI delta) |
| `kMemfaultMetricType_Timer` | Time-in-state within a heartbeat window |
| `kMemfaultMetricType_String` | Board ID, firmware variant (max 64 bytes) |

The SDK resets all accumulators and timers at each heartbeat boundary
(`CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS`, default 3600 s).

### Naming conventions

| Prefix | Category | Example |
|--------|----------|---------|
| `ncs_` | NCS/Zephyr infrastructure (heap, stack watermarks) | `ncs_system_heap_used` |
| `ncs_wifi_` | Wi-Fi stack driver threads | `ncs_wifi_hostap_iface_unused_stack` |
| `ncs_mflt_` / `memfault_` | Memfault infrastructure threads | `memfault_upload_unused_stack` |
| `app_` | Application-level protocol counters | `app_https_req_fail_count` |

New application metrics should use the `app_` prefix. New NCS infrastructure metrics should
use the appropriate `ncs_` sub-prefix.

### Runtime API

```c
#include <memfault/metrics/metrics.h>

/* Increment an unsigned counter */
MEMFAULT_METRIC_ADD(app_my_counter, 1);

/* Set to an absolute value */
MEMFAULT_METRIC_SET_UNSIGNED(app_my_counter, value);

/* Timer: measure time-in-state within the heartbeat window */
MEMFAULT_METRIC_TIMER_START(app_wifi_connected_time);
MEMFAULT_METRIC_TIMER_STOP(app_wifi_connected_time);
```

### Current metric categories

| Category | Keys | Active condition |
|----------|------|-----------------|
| Application demo | `switch_1_toggle_count` | always |
| System heap | `ncs_system_heap_{total,used,peak}` | `CONFIG_HEAPS_MONITOR && CONFIG_HEAP_MEM_POOL_SIZE > 0` |
| mbedTLS heap | `ncs_mbedtls_heap_{total,used,peak}` | `CONFIG_HEAPS_MONITOR && CONFIG_MBEDTLS_ENABLE_HEAP` |
| Wi-Fi stack threads | `ncs_wifi_{hostap_iface,hostap_handler,intr,bh}_unused_stack` | always |
| Network threads | `ncs_{mflt_http,mqtt_helper,conn_mgr_monitor,net_socket_service,rx_q0,tx_q0,net_mgmt,tcp_work}_unused_stack` | always |
| System threads | `ncs_{shell_uart,logging,main}_unused_stack` | always |
| Memfault threads | `{memfault_upload,mflt_ota_triggers}_unused_stack` | always |
| App HTTPS | `app_https_client_unused_stack`, `app_https_req_{total,fail}_count` | `CONFIG_APP_HTTPS_CLIENT_MODULE` |
| App MQTT | `app_mqtt_client_unused_stack`, `app_mqtt_echo_{total,fail}_count` | `CONFIG_APP_MQTT_CLIENT_MODULE` |
| App memfault diagnostics | `mflt_log_buf_bytes_used` | always (set at heartbeat boundary and after log-state restore) |

---

## API / Public Interface

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
| Wi-Fi disconnect CDR persist | `Collecting nRF70 CDR data for disconnect-time persist...` → `Disconnect CDR state persisted to flash (N bytes)` on UART | CDR blob saved to `mflt-cdr-state` partition; N matches nRF70 fw stats size |
| Reconnect after CDR persist (no reboot) | `Disconnect-time CDR state restored — uploading to Memfault` on UART; CDR file visible in Memfault Platform → Issues → CDR | CDR blob restored from flash, uploaded on first connect |
| Reboot after CDR persist → reconnect | same UART log + CDR visible in Memfault Platform | CDR blob survived power cycle via external flash |
| CDR collect failure (driver unavailable) | `nRF70 CDR collect failed for flash persist: -N` UART warning | graceful skip; no flash write; no crash |
| Reboot after disconnect → firmware update → reconnect | no restored logs (size mismatch discards blob); normal upload only | safe discard; no crash; expected after OTA |
| Memfault event timestamp (post NTP sync) | Memfault log file captured_date matches wall clock ± 2 s | NTP sync must precede the event; `CONFIG_MEMFAULT_SYSTEM_TIME_SOURCE_CUSTOM=y` required |
| Button 1 short | `Button 1 short press: Memfault heartbeat + upload` | heartbeat metric increment + upload triggered in upload thread |
| Button 2 short | OTA check trigger log | Memfault OTA check invoked |
| Button long press demos | crash trigger log | expected fault path for validation builds |
