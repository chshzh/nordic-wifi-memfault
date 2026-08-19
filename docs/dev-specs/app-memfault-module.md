# App Memfault Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-08-19-16-05 |
| PRD Version | 2026-08-19-15-45 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Status | Implemented |

> `Version` = this spec's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this spec tracks. The two normally **differ** — never set them equal.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Replaces `pm/openspec/specs/memfault-integration.md`. Confirmed unchanged core upload/DNS-wait/button-action behavior in `core/memfault_core.c`. Documented NCS v2.6.4-specific API differences: `memfault_metrics_connectivity_connected_state_change()` and `CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS` do not exist in the bundled Memfault SDK version; `mflt_nrf70_fw_stats_cdr.c` renamed `nrf70_fw_stats_cdr.c` and ported to `struct rpu_op_stats` / `nrf_wifi_fmac_stats_get()` API. |
| 2026-07-13-12-22 | Updated to PRD v2026-07-13-12-22: added design for FR-102 (`CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`) and FR-103 (`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`), ported from `nordic-wifi-memfault-main`'s FR-007/FR-008. Design only — not yet implemented on this branch; see Open Issues. |
| 2026-07-13-13-31 | FR-102/FR-103 implemented and build-verified (nRF7002DK: FLASH 90.26%, RAM 98.75%). FR-102's design changed from the original raw-ring-buffer-copy plan to a drain-and-replay approach (`memfault_log_read()` on disconnect + `memfault_log_save_preformatted()` on reconnect) because `memfault_log_get_state()`/`memfault_log_restore_state()` do not exist in the Memfault SDK v1.6.0 bundled with NCS v2.6.4. Scratch buffer capped at 4 KB (not the full 12 KB partition) to fit the RAM budget. FR-103 implemented as originally designed. New files: `core/memfault_log_state_restore.c(.h)`. |
| 2026-07-24-11-30 | **FR-102 correctness fix**: the drain in `memfault_log_state_persist_now()` used to stop permanently once the 4 KB scratch buffer filled, keeping the *oldest* unread entries; when Memfault chunk uploads had been failing (leaving backlog), old low-value entries (e.g. heap-monitor prints) could consume the whole budget before the drain ever reached the entries generated near the actual disconnect — e.g. `=== WiFi DISCONNECTED (reason: N) ===` could go missing from the uploaded blob. Now evicts the oldest retained entry (self-describing via its own header, no extra RAM) when full instead of stopping, so a rolling window of the *newest* entries always survives regardless of backlog size. Logs `"Log-state blob full, kept newest %u of %u entries"` only when eviction occurred. **New (opt-in) `CONFIG_APP_MEMFAULT_HEARTBEAT_FORCE_INTERVAL_SEC`** (default `0`/disabled): periodically forces `memfault_metrics_heartbeat_debug_trigger()` + `memfault_log_trigger_collection()` + upload while connected, for testing only — this SDK version's metrics heartbeat is a fixed 3600 s timer, and Memfault logs are never uploaded at all unless something calls `memfault_log_trigger_collection()` (normally only done here after a disconnect/reconnect restore), so without a disconnect cycle neither metrics nor logs would otherwise appear on a useful timescale for manual testing. **DNS reliability**: ported `CONFIG_DNS_NUM_CONCUR_QUERIES=2` and `CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC=60` (staggered from the upload thread's own 30 s connect delay) plus a 2 s initial delay before the DNS-ready poll loop in `upload_thread_fn` from `nordic-wifi-memfault-main`, reducing `DNS lookup ... failed: -11` (EAGAIN from concurrent `getaddrinfo()` collisions) and the DHCP→resolver propagation race on first connect. |
| 2026-07-24-14-09 | **FR-102 persist-overwrite guard fix**: found via live Memfault log analysis (an actual uploaded log-file JSON cross-referenced against `mcp_memfault_device_listReboots` to correlate UART-relative timestamps to absolute UTC) — on a flapping AP, `memfault_log_state_persist_now()` could fire a second time and silently overwrite a still-unrestored blob from an earlier disconnect in the single `mflt_log_state_partition`, permanently losing it before `memfault_log_state_restore_on_connect()` ever ran (restore only happens after a full, DNS-ready reconnect, which a flapping AP may not reach for several disconnect cycles). Root cause: `WIFI_STA_CONNECTED` — which cancels the pending 10 s persist-scheduling work — is only published at `NET_EVENT_IPV4_DHCP_BOUND`, not the earlier L2 "WiFi is connected!" event, so a transient IP-loss blip during reassociation can let the persist work fire before the real reconnect cancels it. Fix: `memfault_log_state_persist_now()` now checks the flash partition header first and returns `-EALREADY` (skipping the persist, with a warning) if a valid unrestored blob is already present, instead of overwriting it. **FR-104 cross-reference**: added `ntp-module.md` (SNTP time sync) — once it completes its first sync, restored log-state entries (and the original disconnect-time entries) carry real UTC time instead of the restore-time approximation noted below. |
| 2026-08-19-10-45 | **New (opt-in) periodic CDR collection and coredump retry**, both default `0`/disabled in `Kconfig.defaults`: (1) `CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC` collects a fresh nRF70 FW stats snapshot on a timer (`cdr/nrf70_fw_stats_cdr.c`), same code path as the Button-1 trigger, so a CDR blob is ready whenever the periodic HTTP upload or an on-connect upload fires instead of only after a button press or a disconnect event. **Cloud rate limit still applies**: Memfault accepts at most 1 CDR upload per device per 24 hours, so this does not produce more than one CDR data point per day — it only keeps whichever snapshot is ready as fresh as possible. (2) `CONFIG_APP_MEMFAULT_COREDUMP_PERIODIC_CHECK_INTERVAL_SEC` re-checks `memfault_coredump_has_valid_coredump()` on a timer and retries `memfault_zephyr_port_post_data()` (`core/memfault_core.c`) if one is still pending, since previously a coredump only got a single upload attempt at `on_connect()` with no automatic retry if that attempt was skipped (e.g. TLS heap busy). Since `memfault_zephyr_port_post_data()` posts all ready Memfault data together (metrics, logs, CDR, coredump), this also gives the periodic CDR snapshot above an extra upload opportunity on the same cadence. Both features required moving each work item's `K_WORK_DELAYABLE_DEFINE` before its handler function (with a forward declaration), matching the existing `heap_monitor.c` pattern, since the handler reschedules itself. |
| 2026-08-19-10-55 | **Regression found and reverted**: this project's `prj.conf` initially set both new options above to `900` (15 min). That build reported RAM 99.56% used and *linked/built successfully*, but left only 2020 B free in `sram_primary` — below `CONFIG_NEWLIB_LIBC_MIN_REQUIRED_HEAP_SIZE` (2048 B, Newlib is forced on via `CONFIG_NEWLIB_LIBC=y`, required for hostap). This is a **runtime boot assertion, not a link error**, so `west build` gave no warning; the device only failed at first boot, in a hard assert-reboot loop (`ASSERTION FAIL [...] memory space available for newlib heap is less than the minimum required size...`, `zephyr/lib/libc/newlib/libc-hooks.c:128`), confirmed on real hardware. Reverted `prj.conf` to leave both at their Kconfig default of `0` (disabled) — rebuilt at RAM 99.54%, 2116 B free (only 68 B above the 2048 B floor). **Both features remain fully implemented and available to enable**, but only after a dedicated RAM optimization pass frees real margin — enabling either (let alone both) at 900 s again on this exact build will very likely reproduce the same bootloop. Lesson: on this target, `west build`'s reported "RAM: XX.XX% used" is **not sufficient** to confirm a change is safe — the actual pass/fail threshold is whatever's left after `_end` versus `CONFIG_NEWLIB_LIBC_MIN_REQUIRED_HEAP_SIZE`, which only surfaces at runtime on hardware. |
| 2026-08-19-11-05 | **Partial re-enable, pending hardware confirmation**: measured that each `k_work_delayable` costs exactly 48 B RAM (`(456732-456636)/2` bytes comparing the both-enabled and both-disabled builds). Set only `CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC=900` in `prj.conf`, left `CONFIG_APP_MEMFAULT_COREDUMP_PERIODIC_CHECK_INTERVAL_SEC` at its default `0` (commented out) — this pays for one 48 B struct instead of two. Rebuilt: RAM 99.55%, exactly **2068 B free — only 20 B above the 2048 B newlib floor**. This is not a comfortable margin (a single additional log string or static buffer anywhere in the app could reproduce the 2026-08-19-10-45 bootloop again), so this configuration is **build-verified but not yet hardware-boot-verified** as of this entry — confirm the device boots cleanly on real hardware before relying on it, and treat any future RAM-affecting change on this target as needing a fresh hardware boot test, not just a passing `west build`. |
| 2026-08-19-13-00 | **Removed periodic coredump retry entirely** (`CONFIG_APP_MEMFAULT_COREDUMP_PERIODIC_CHECK_INTERVAL_SEC`, its work item in `core/memfault_core.c`, and all `prj.conf`/doc references) — this feature does not make sense for coredump data and was redundant even before the RAM cost was considered. Reasons: (1) **A coredump is an immutable, one-time snapshot** frozen at the exact instant of a crash — unlike the live, continuously-changing nRF70 CDR stats, there is nothing to "refresh" by re-checking it periodically; the bytes are identical on every check until it's actually uploaded and erased. (2) **A coredump already lives in flash**, written by the fault handler at crash time (`memfault_flash_coredump_storage.c`) — there was never anything to "collect into RAM" the way `mflt_nrf70_fw_stats_cdr_collect()` actively pulls a live snapshot; the removed work item only ever re-checked a flash header and retried the upload call, never moved any data. (3) **The retry it provided was already redundant**: this app's `CONFIG_MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS=900` (SDK-level, `ports/zephyr/common/memfault_http_periodic_upload.c`, already running as part of the base Memfault integration, zero extra RAM cost) calls `memfault_zephyr_port_post_data()` whenever `memfault_packetizer_data_available()` is true — and the coredump data source (`g_memfault_coredump_data_source`, registered in `memfault_data_packetizer.c`) is one of the sources that function already checks. A pending coredump was therefore **already being retried automatically every 900 s for free**, before this feature ever existed; the dedicated work item duplicated that behavior at a real RAM cost (48 B) this target could not spare (it was the direct cause of the 2026-08-19-10-45 bootloop when combined with the CDR option). Net effect of removal: no loss of functionality, one `k_work_delayable` (48 B) of RAM headroom returned. |
| 2026-08-19-16-05 | **Removed nRF54LM20DK + nRF7002EB II support project-wide** — see [1-architecture.md](1-architecture.md) Changelog for the full removal. This directly resolves the long-standing Open Issue below about the nRF54LM20DK "BOARD_ROOT environment issue" (first flagged 2026-07-13): that board build was never actually verifiable in this environment because no board definition for it exists anywhere in this NCS v2.6.4 installation — the issue was never going to be "fixed", the board genuinely isn't supported here. `CONFIG_MEMFAULT_NCS_FW_TYPE`/`CONFIG_MEMFAULT_NCS_HW_VERSION` board-conditional defaults simplified to nRF7002DK-only. |

---

## Overview

`app_memfault` is the library-wrapper module group for the Memfault SDK. It has four
sub-areas under one Kconfig group (`CONFIG_APP_MEMFAULT_MODULE`):

- **core** (`core/memfault_core.c`) — boot image confirmation, Wi-Fi-connect-driven upload
  thread (with bounded DNS wait), and `BUTTON_CHAN`/`WIFI_CHAN` listeners that turn button
  presses into heartbeat triggers, OTA-check requests, and crash demos.
- **metrics** (`metrics/wifi_metrics.c`, `metrics/stack_metrics.c`) — heartbeat data
  collectors invoked from `memfault_metrics_heartbeat_collect_data()`.
- **ota** (`ota/ota_triggers.c`) — a dedicated thread that calls `memfault_fota_start()` on
  button press, Wi-Fi connect, or a periodic timer.
- **cdr** (`cdr/nrf70_fw_stats_cdr.c`) — nRF70 Wi-Fi firmware PHY/LMAC/UMAC statistics
  packaged as a Memfault Custom Data Recording, triggered alongside the Button-1
  short-press heartbeat.

**Log-state persist/restore across power cycle** (FR-102, `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`,
default `y`): on `WIFI_STA_DISCONNECTED` (`WIFI_CHAN`) or `NETWORK_NOT_READY` (`NETWORK_CHAN`) —
guarded so it only fires once per disconnect event and only after the device has completed at
least one real connect (`network_ever_connected`) — a 10 s delayable work item drains unread
entries from the live Memfault log ring buffer one at a time via `memfault_log_read()` and
serializes them (level + type + message bytes) to the dedicated external-flash
`mflt_log_state_partition` (12 KB, see [2-pm-partition.md](2-pm-partition.md)). A 4 KB scratch
buffer bounds how much can be drained per disconnect (RAM budget is tight on this target); if the
unread backlog exceeds the buffer, the drain evicts the oldest retained entry to make room for
each new one, so the persisted blob is always a rolling window of the **newest** entries rather
than whatever was queued first — this matters when Memfault uploads have been failing and old
low-value log lines would otherwise crowd out the entries closest to the disconnect. On the next
Wi-Fi reconnect,
`on_connect()` calls `memfault_log_state_restore_on_connect()`, which replays each entry back into
the live ring buffer via `memfault_log_save_preformatted()`, calls
`memfault_log_trigger_collection()` to mark them for upload, writes a visual separator log line
(`=== [LOG RESTORE] pre-disconnect logs above | live session below ===`), then erases the
partition. A truncated or invalid blob is discarded silently, no crash. Ported from
`nordic-wifi-memfault-main` FR-007, with one behavioral difference: because the Memfault SDK
version bundled with NCS v2.6.4 (v1.6.0) has no raw ring-buffer save/restore API
(`memfault_log_get_state()`/`memfault_log_restore_state()` do not exist), this module
replays entries individually instead of copying raw ring-buffer memory. Separately, Memfault's
`memfault_log_trigger_collection()` timestamps the *whole batch* of unsent logs with one shared
value captured at trigger time (`memfault_log_data_source.c`), not a per-line timestamp — so a
restored batch is always stamped with the restore-time, never each line's exact original time,
regardless of NTP. Before [ntp-module.md](ntp-module.md) (FR-104) existed, this app had no other
real-time source (`CONFIG_DATE_TIME`/`CONFIG_RTC`), so that restore-time timestamp — like every
other Memfault event/log — was never actually attached at all; Memfault's backend fell back to
its own ingest-time stamp. Once FR-104 has completed its first sync, restored (and all other)
batches get a real device UTC restore-time timestamp instead of that ingest-time fallback — an
accuracy improvement, but still not per-line original-time precision. To prevent a second
disconnect cycle from overwriting a still-unrestored blob (observed on a flapping AP, where
restore may not be reached for several disconnect cycles), `memfault_log_state_persist_now()`
checks the flash partition header first and skips the persist (`-EALREADY`) if a valid
unrestored blob is already present, rather than clobbering it.

**CDR persist/restore across power cycle** (FR-103, `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`,
default `y` when `NRF70_FW_STATS_CDR_ENABLED`): when the same 10 s disconnect work item fires,
immediately after the log-state persist, a fresh nRF70 firmware-stats snapshot is collected and
written (16-byte header + raw blob) to the dedicated external-flash `mflt_cdr_state_partition`
(8 KB). On the next reconnect, the blob is restored into the CDR module's internal buffer so the
existing button-triggered CDR upload path (FR-101 / `has_cdr_cb`) picks it up on the next
`memfault_zephyr_port_post_data()` call, then the partition is erased. Oversized blobs are
discarded with a warning; an nRF70 driver unavailable at collect time returns `-ENODEV` with no
flash write. Depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED`. Ported directly from
`nordic-wifi-memfault-main` FR-008, unchanged behavior.

**Periodic CDR collection** (`CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC`, default `0`/
disabled, set to `900` in this project's `prj.conf` as of 2026-08-19-11-05, pending hardware
boot confirmation — see RAM warning below): a `k_work_delayable` in `cdr/nrf70_fw_stats_cdr.c`
calls the same `mflt_nrf70_fw_stats_cdr_collect()` used by the Button-1 trigger on a fixed
interval, independent of button presses or disconnect events, so a CDR blob is normally ready
whenever any upload (periodic HTTP or on-connect) fires. `-ENODEV` (RPU/WiFi driver not yet up)
is treated as expected and silent; any other collection failure logs a warning. **The Memfault
cloud still enforces its own 1-CDR-upload-per-device-per-24h limit regardless of how often this
collects** — see the CDR Kconfig `WARNING` below.

**No periodic coredump collection** (removed 2026-08-19-13-00, see Changelog): unlike the CDR
snapshot above, a coredump does not benefit from periodic re-checking. It's an immutable snapshot
written once, at crash time, directly to flash by the fault handler — there is no live data to
refresh. A pending coredump upload is already retried automatically, for free, by the SDK's own
`CONFIG_MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS` timer (`memfault_http_periodic_upload.c`),
which checks `memfault_packetizer_data_available()` — a check that already includes the
registered coredump data source. A dedicated app-level periodic work item for this would only
duplicate that existing behavior, at a real RAM cost this target cannot spare.

---

## Location

- **Path**: `src/modules/app_memfault/`
- **Files**: `Kconfig.app_memfault`, `Kconfig.defaults`, `CMakeLists.txt`, plus subdirectories `core/`, `metrics/`, `ota/`, `cdr/`, `config/` (`memfault_metrics_heartbeat_config.def`)
- **FR-102/FR-103**: `core/memfault_log_state_restore.c(.h)` (built when `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE=y`, default `y`); `cdr/nrf70_fw_stats_cdr.c` gains flash persist/restore functions (built when `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE=y`, default `y` when CDR is enabled)

---

## Module Type

- [ ] Application module (no SMF state machine of its own)
- [x] **Library wrapper module** — wraps the Memfault SDK (`CONFIG_MEMFAULT`, selected by `CONFIG_APP_MEMFAULT_MODULE`).

---

## External Library Interface

| Field | Value |
|-------|-------|
| Library | Memfault SDK (NCS-bundled `nrfconnect_port`) |
| NCS Kconfig | `CONFIG_APP_MEMFAULT_MODULE=y` → `select MEMFAULT` |
| Library internal threads | Memfault SDK's own HTTP/FOTA transfer plumbing (invoked synchronously from this module's own threads below, not a persistent background thread of its own) |

**APIs called by this module** (app → library):

```c
/* core/memfault_core.c */
boot_is_img_confirmed(); boot_write_img_confirmed();     /* MCUboot image confirm */
memfault_log_set_min_save_level(kMemfaultPlatformLogLevel_Debug);
memfault_metrics_heartbeat_debug_trigger();               /* force a heartbeat now */
memfault_packetizer_data_available(); memfault_zephyr_port_post_data();  /* upload */
memfault_coredump_has_valid_coredump(NULL);
MEMFAULT_METRIC_ADD(switch_1_toggle_count, 1);             /* Button 3 demo metric */
MEMFAULT_TRACE_EVENT_WITH_LOG(switch_2_toggled, ...);      /* Button 4 demo trace */
#if CONFIG_MEMFAULT_NCS_STACK_METRICS
memfault_ncs_metrics_collect_data();                       /* stack metrics (NCS port) */
#endif

/* ota/ota_triggers.c */
memfault_fota_start();                                     /* check + start OTA download */

/* cdr/nrf70_fw_stats_cdr.c */
/* Packages nRF70 stats via the Memfault CDR (Custom Data Recording) API */
```

**Callbacks implemented by this module** (library → app):

```c
/* Required by the Memfault SDK: invoked when a heartbeat is about to be captured */
void memfault_metrics_heartbeat_collect_data(void)
{
    memfault_ncs_metrics_collect_data();  /* if CONFIG_MEMFAULT_NCS_STACK_METRICS */
    mflt_wifi_metrics_collect();
}
```

**Zbus integration** — how library events / triggers translate into Zbus subscriptions:

| Library event / trigger | Zbus channel subscribed | Behavior |
|--------------------------|----------------------|---------|
| Wi-Fi connectivity change | `WIFI_CHAN` (`memfault_wifi_listener_def`) | `WIFI_STA_CONNECTED` → init stack metrics, `k_sem_give(&upload_sem)` to wake the upload thread. `WIFI_STA_DISCONNECTED` → clear `wifi_connected` flag. |
| Button action | `BUTTON_CHAN` (`memfault_button_listener_def`) | See Button Actions table below. |
| Button 2 short press / Wi-Fi connect | `BUTTON_CHAN`, `WIFI_CHAN` (`ota_button_listener`, `ota_wifi_listener` in `ota_triggers.c`) | Sets a flag bit and wakes `mflt_ota_triggers_tid`. |
| **[Planned FR-102/FR-103]** Wi-Fi/network connectivity loss | `WIFI_CHAN` (`WIFI_STA_DISCONNECTED`), `NETWORK_CHAN` (`NETWORK_NOT_READY`) | Sets a `log_freeze_scheduled` guard (once per disconnect burst, only if `network_ever_connected`) and schedules a 10 s delayable work item that persists log-state and CDR to external flash. |
| **[Planned FR-102/FR-103]** Wi-Fi reconnect | `WIFI_CHAN` (`WIFI_STA_CONNECTED`) | Clears `log_freeze_scheduled`, cancels any pending persist work, then restores log-state and CDR from external flash in `on_connect()` before upload. |

---

## Zbus Integration

**Subscribes to**: `WIFI_CHAN`, `BUTTON_CHAN` (both `core` and `ota` sub-areas register their own listeners).

**Publishes to**: none — this module is a Zbus consumer only.

### Button Actions (via `memfault_button_listener`, `core/memfault_core.c`)

| Button | Press | Action |
|--------|-------|--------|
| 1 | Short (< `BUTTON_LONG_PRESS_THRESHOLD_MS`) | `memfault_metrics_heartbeat_debug_trigger()` + `memfault_zephyr_port_post_data()` if `wifi_connected`, else log warning |
| 1 | Long | `fib(10000)` — deliberate stack overflow to exercise coredump capture |
| 2 | Long | `i = 1 / 0` (compiler warning suppressed) — deliberate division-by-zero fault |
| 2 | Short | Handled by `ota_triggers.c`, not `core` — see below |
| 3 | Short | `MEMFAULT_METRIC_ADD(switch_1_toggle_count, 1)` |
| 4 | Short | `MEMFAULT_TRACE_EVENT_WITH_LOG(switch_2_toggled, "Switch state: 1")` |

### DNS-Wait Upload Flow (`core/memfault_core.c`, `upload_thread_fn`)

```mermaid
sequenceDiagram
    participant Network as network module
    participant Core as memfault_core (upload thread)
    participant DNS
    participant SDK as Memfault SDK

    Network->>Core: WIFI_CHAN: WIFI_STA_CONNECTED
    Core->>Core: wifi_connected = true; k_sem_give(upload_sem)
    Core->>Core: upload_thread_fn wakes (k_sem_take)
    loop every 10s, up to 300s
        Core->>DNS: getaddrinfo("chunks-nrf.memfault.com", "443")
        alt resolves
            DNS-->>Core: success
        else still pending
            Core->>Core: k_sleep(10s), continue
        end
    end
    Core->>SDK: memfault_metrics_heartbeat_debug_trigger()
    Core->>SDK: memfault_zephyr_port_post_data() (only if data available)
```

### OTA Trigger Flow (`ota/ota_triggers.c`)

- Triggers: Button 2 short press, `WIFI_STA_CONNECTED`, or a periodic timer
  (`CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN`, default 60 min).
- On wake, sleeps `CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC` (default per Kconfig help: 60 s) to
  let DNS settle, then calls `memfault_fota_start()`.
- Trigger context (`button`, `connect`, `button+connect`, or `periodic`) is logged for
  diagnostics.

---

## State Machine

Not SMF. `core` uses a Zbus-listener + dedicated-thread pattern (`memfault_upload_tid`);
`ota` uses a dedicated thread (`mflt_ota_triggers_tid`) woken by a semaphore + bitflags.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|--------------|
| `CONFIG_APP_MEMFAULT_MODULE` | bool | project-enabled | Enables the whole group; `select MEMFAULT` |
| `CONFIG_NRF70_FW_STATS_CDR_ENABLED` | bool | project-enabled | Enable nRF70 stats CDR (`depends on MEMFAULT_CDR_ENABLE`, `WIFI_NRF700X`) |
| `CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE` | int | project-set (recommended ≥ 6144 B) | Stack for the DNS-wait/upload thread (`getaddrinfo` + TLS/HTTPS) |
| `CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN` | int (1–1440) | `60` | Periodic OTA check interval |
| `CONFIG_MEMFAULT_OTA_CONNECT_DELAY_SEC` | int (0–300) | `60` | Delay before an OTA check after a trigger, to let DNS settle |
| `CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE` | int | `4096` (per Kconfig help) | Stack for the OTA trigger thread |
| `CONFIG_MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS` | int | `60` (`prj.conf`) | Memfault SDK's own periodic upload interval |
| `CONFIG_APP_MEMFAULT_HEARTBEAT_FORCE_INTERVAL_SEC` | int (0–3600) | `0` (disabled); `60` in current test `prj.conf` | Testing aid only: periodically forces a heartbeat + log collection + upload while connected, since this SDK's metrics heartbeat is a fixed 3600 s timer and logs are never uploaded without an explicit `memfault_log_trigger_collection()` call. Leave `0` for production. |
| `CONFIG_MEMFAULT_NCS_FW_VERSION_STATIC` | choice | selected | Firmware version source is a static string, not build-derived |
| `CONFIG_MEMFAULT_ROOT_CERT_STORAGE_TLS_CREDENTIAL_STORAGE` | choice | selected | Cert storage backend |
| `CONFIG_MEMFAULT_NRF_CONNECT_SDK` | bool | `y` (explicit in `prj.conf`) | Required on this target to get NCS FOTA/reboot-reason port extensions — NCS v2.6.4 does not default this to `y` for the nRF7002DK target class |
| `CONFIG_MEMFAULT_FS_BYTES_FREE_METRIC` | bool | `n` (`prj.conf`) | Disabled — app has no mounted VFS filesystem; avoids `fs: mount point not found!!` spam |
| `CONFIG_MEMFAULT_NCS_PROJECT_KEY` | string | set via git-ignored `overlay-app-memfault-project-info.conf` | Memfault project key — never committed |
| `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE` | bool | **planned**, n today | [FR-102] Persist Memfault ring-buffer state to `mflt_log_state_partition` on disconnect; restore and upload on next Wi-Fi connect |
| `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` | bool | **planned**, n today | [FR-103] Persist disconnect-time nRF70 CDR blob to `mflt_cdr_state_partition`; restore and upload on next Wi-Fi connect. Depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED` |
| `CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC` | int (0–86400) | `0` (disabled); non-zero in current `prj.conf` — see RAM warning below | Collect a fresh nRF70 CDR snapshot every N seconds, independent of Button-1/disconnect triggers. Cloud still caps actual uploads to 1/device/24h regardless. Depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED` |

> **RAM warning**: this option adds one static `k_work_delayable` (measured 48 B). A previously-implemented sibling option for periodic coredump retry was removed entirely (see Changelog 2026-08-19-13-00) after it, combined with this one, caused a boot-time newlib-heap assertion loop on nRF7002DK (2020 B free vs. a 2048 B floor) — that removal was not just a RAM workaround, the feature itself was redundant (see Overview). Re-verify actual free RAM on hardware before enabling anything else RAM-resident on this target (see [3-memopt.md](3-memopt.md)).

---

## API / Public Interface

`core`, `ota`, `metrics`, and `cdr` expose no public functions to other application modules —
all interaction is via Zbus (`BUTTON_CHAN`, `WIFI_CHAN`) or the Memfault SDK's own callback
contract (`memfault_metrics_heartbeat_collect_data`).

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| DNS never resolves | `check_dns_ready()` loop reaches `DNS_TIMEOUT_SEC` (300 s) | Log error, upload proceeds anyway |
| Wi-Fi disconnects during DNS wait | `wifi_connected` flag cleared mid-loop | Abort this upload attempt, `continue` to wait for the next connect |
| `boot_write_img_confirmed()` fails | Return code checked in `memfault_core_init` | Log error; boot continues (image stays unconfirmed, MCUboot may roll back next boot) |
| OTA disabled at compile time | `#if IS_ENABLED(CONFIG_MEMFAULT_FOTA)` false in `ota_triggers.c` | Logs a one-time warning instead of calling `memfault_fota_start()` |
| **FR-102** Restored log blob size mismatch or truncation | Blob magic/version/entry-count invalid, or replay would read past `payload_len` | Blob discarded (or replay stopped early), no crash |
| **FR-103** nRF70 driver unavailable at persist time | `mflt_nrf70_fw_stats_cdr_persist_to_flash()` returns `-ENODEV` | Warning logged; no flash write performed |
| **FR-103** Restored CDR blob oversized | Blob size exceeds `NRF70_FW_STATS_BLOB_MAX_SIZE` | Blob discarded with a warning |
| Periodic CDR collection fires before RPU/WiFi driver is up | `mflt_nrf70_fw_stats_cdr_collect()` returns `-ENODEV` | Silent (expected at early boot); next periodic tick retries |
| Periodic CDR collection fires while a previous snapshot is still unuploaded | `s_cdr_data_ready` already `true` | Warning logged, snapshot overwritten (newest always wins, same as the Button-1 path) |
---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~120 KB total (core+metrics+OTA+CDR) | Per legacy architecture estimate; whole-app build measured at FLASH 91.03% / RAM 99.55% (2068 B free) on nRF7002DK as of 2026-08-19-13-00, with `CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC` enabled (non-zero) and no coredump periodic option (removed, see Changelog) |
| RAM (static) | ~46 KB | Per legacy estimate. The one remaining periodic option (CDR) costs 48 B (one `k_work_delayable`) — 2068 B free, only 20 B above the 2048 B newlib floor. **Do not add any further RAM-resident feature to this app** without freeing real margin first (see Open Issues, [3-memopt.md](3-memopt.md)) |
| Stack | `CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE` + `CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE` | Two dedicated `K_THREAD_DEFINE` threads |
| Coredump storage | 64 KB | `memfault_storage` (nRF7002DK) — see [2-pm-partition.md](2-pm-partition.md) |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Boot confirm | `New OTA FW confirmed!` | First boot after an OTA update |
| Wi-Fi connect → upload | `Connected to network` → `Waiting for DNS resolver to be ready for Memfault` → `Sending already captured data to Memfault` | After `WIFI_STA_CONNECTED` |
| Button 1 short | (heartbeat trigger, no dedicated core log line beyond `LOG_INF("Button 1 short press: Memfault heartbeat")`) | duration < long-press threshold, `wifi_connected == true` |
| Button 1 long | `Stack overflow will now be triggered` | duration ≥ long-press threshold |
| Button 2 long | `Division by zero will now be triggered` | duration ≥ long-press threshold |
| OTA check | `Starting Memfault OTA check (<context>)` | button / connect / periodic trigger |
| **FR-102** Log restore | `=== [LOG RESTORE] pre-disconnect logs above \| live session below ===` in Memfault cloud log view | after reconnect following a power cycle with a persisted blob |
| Periodic CDR collection | `nRF70 FW stats CDR ready for upload (N bytes)` | Every `CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC`, once RPU/WiFi driver is up |

---

## Open Issues / TBD

- [ ] `memfault_metrics_connectivity_connected_state_change()` does not exist in the Memfault SDK version bundled with NCS v2.6.4 — connectivity-state metrics are tracked only via the local `wifi_connected` bool, not reported through that API.
- [ ] `CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS` does not exist on this SDK version — heartbeat interval is fixed by the bundled SDK default, not configurable via this Kconfig.
- [ ] Re-run the memory estimate against a live NCS v2.6.4 build on nRF7002DK (see [3-memopt.md](3-memopt.md)).
- [x] **FR-102/FR-103**: confirmed `memfault_log_get_state()`/`memfault_log_restore_state()` do **not** exist in the Memfault SDK v1.6.0 bundled with NCS v2.6.4; implemented FR-102 via a drain-and-replay approach (`memfault_log_read()` + `memfault_log_save_preformatted()`) instead of a raw-state copy — see the trade-off noted above.
- [x] **FR-102/FR-103**: confirmed the external-flash driver (`flash_area_*` on `MX25R64` via SPI) works from `on_connect()`/the disconnect work item, and the new `mflt_log_state_partition`/`mflt_cdr_state_partition` regions do not collide with `mcuboot_secondary` — build-verified on nRF7002DK (FLASH 90.26%, RAM 98.75%).
- [ ] **RAM headroom critical (99.55% / 2068 B free on nRF7002DK, CDR periodic collection enabled, no other periodic options)**. Confirmed on real hardware: a previous configuration that additionally enabled a (since-removed) periodic coredump-retry option dropped free RAM below `CONFIG_NEWLIB_LIBC_MIN_REQUIRED_HEAP_SIZE` (2048 B) and the device hard-looped on a boot-time assertion in `newlib/libc-hooks.c` — it never reached application code. `west build`'s "RAM: XX% used" summary does **not** catch this; it's a link-time-silent, runtime-only failure that only showed up flashing real hardware. The current config (CDR periodic collection only) is build-verified at 2068 B free (20 B margin) — treat this margin as fragile: any further RAM-resident addition to this app needs a fresh [3-memopt.md](3-memopt.md) pass first to find real margin, then a hardware boot test to confirm — do not trust the linker's "% used" figure alone on this target.
- [ ] Confirm on real hardware that the current build (CDR periodic collection enabled, 2068 B free) boots cleanly — not yet hardware-boot-verified as of 2026-08-19-13-00.

---

## Related Specs

- [button-module.md](button-module.md) — publishes the `BUTTON_CHAN` events this module consumes
- [network-module.md](network-module.md) — publishes the `WIFI_CHAN` events this module consumes
- [ntp-module.md](ntp-module.md) — provides real UTC time once synced, used by restored FR-102 log-state entries
- [heap-monitor-module.md](heap-monitor-module.md) — feeds heap metrics into the same Memfault heartbeat
- [2-pm-partition.md](2-pm-partition.md) — coredump partition layout

*(Changelog is maintained at the top of this document.)*
