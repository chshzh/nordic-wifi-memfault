# App Memfault Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-07-13-13-31 |
| PRD Version | 2026-07-13-12-22 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB II |
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
buffer bounds how much can be drained per disconnect (RAM budget is tight on this target); the
drain stops early with a warning if more unread data remains. On the next Wi-Fi reconnect,
`on_connect()` calls `memfault_log_state_restore_on_connect()`, which replays each entry back into
the live ring buffer via `memfault_log_save_preformatted()`, calls
`memfault_log_trigger_collection()` to mark them for upload, writes a visual separator log line
(`=== [LOG RESTORE] pre-disconnect logs above | live session below ===`), then erases the
partition. A truncated or invalid blob is discarded silently, no crash. Ported from
`nordic-wifi-memfault-main` FR-007, with one behavioral difference: because the Memfault SDK
version bundled with NCS v2.6.4 (v1.6.0) has no raw ring-buffer save/restore API
(`memfault_log_get_state()`/`memfault_log_restore_state()` do not exist), this module
replays entries individually instead of copying raw ring-buffer memory — so restored entries
carry the restore-time (NTP) timestamp, not the exact original disconnect-time timestamp.

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
| `CONFIG_MEMFAULT_NCS_FW_VERSION_STATIC` | choice | selected | Firmware version source is a static string, not build-derived |
| `CONFIG_MEMFAULT_ROOT_CERT_STORAGE_TLS_CREDENTIAL_STORAGE` | choice | selected | Cert storage backend |
| `CONFIG_MEMFAULT_NRF_CONNECT_SDK` | bool | `y` (explicit in `prj.conf`) | Required on this target to get NCS FOTA/reboot-reason port extensions — NCS v2.6.4 does not default this to `y` for the nRF7002DK/nRF54LM20DK target class |
| `CONFIG_MEMFAULT_FS_BYTES_FREE_METRIC` | bool | `n` (`prj.conf`) | Disabled — app has no mounted VFS filesystem; avoids `fs: mount point not found!!` spam |
| `CONFIG_MEMFAULT_NCS_PROJECT_KEY` | string | set via git-ignored `overlay-app-memfault-project-info.conf` | Memfault project key — never committed |
| `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE` | bool | **planned**, n today | [FR-102] Persist Memfault ring-buffer state to `mflt_log_state_partition` on disconnect; restore and upload on next Wi-Fi connect |
| `CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` | bool | **planned**, n today | [FR-103] Persist disconnect-time nRF70 CDR blob to `mflt_cdr_state_partition`; restore and upload on next Wi-Fi connect. Depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED` |

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

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~120 KB total (core+metrics+OTA+CDR) | Per legacy architecture estimate; not re-measured on NCS v2.6.4 |
| RAM (static) | ~46 KB | Per legacy estimate |
| Stack | `CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE` + `CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE` | Two dedicated `K_THREAD_DEFINE` threads |
| Coredump storage | 64 KB | `memfault_storage` (nRF7002DK) / `memfault_coredump_partition` (nRF54LM20DK) — see [2-pm-partition.md](2-pm-partition.md) |

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

---

## Open Issues / TBD

- [ ] `memfault_metrics_connectivity_connected_state_change()` does not exist in the Memfault SDK version bundled with NCS v2.6.4 — connectivity-state metrics are tracked only via the local `wifi_connected` bool, not reported through that API.
- [ ] `CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS` does not exist on this SDK version — heartbeat interval is fixed by the bundled SDK default, not configurable via this Kconfig.
- [ ] Re-run the memory estimate against a live NCS v2.6.4 build on both boards (see [3-memopt.md](3-memopt.md)).
- [x] **FR-102/FR-103**: confirmed `memfault_log_get_state()`/`memfault_log_restore_state()` do **not** exist in the Memfault SDK v1.6.0 bundled with NCS v2.6.4; implemented FR-102 via a drain-and-replay approach (`memfault_log_read()` + `memfault_log_save_preformatted()`) instead of a raw-state copy — see the trade-off noted above.
- [x] **FR-102/FR-103**: confirmed the external-flash driver (`flash_area_*` on `MX25R64`/`MX25R6435F` via SPI) works from `on_connect()`/the disconnect work item on both boards, and the new `mflt_log_state_partition`/`mflt_cdr_state_partition` regions do not collide with `mcuboot_secondary` — build-verified on nRF7002DK (FLASH 90.26%, RAM 98.75%). nRF54LM20DK build not re-verified this pass (pre-existing `BOARD_ROOT` environment issue unrelated to this change).
- [ ] nRF54LM20DK build for FR-102/FR-103 not yet re-verified in this environment; re-run once the board-root setup is fixed.

---

## Related Specs

- [button-module.md](button-module.md) — publishes the `BUTTON_CHAN` events this module consumes
- [network-module.md](network-module.md) — publishes the `WIFI_CHAN` events this module consumes
- [heap-monitor-module.md](heap-monitor-module.md) — feeds heap metrics into the same Memfault heartbeat
- [2-pm-partition.md](2-pm-partition.md) — coredump partition layout

*(Changelog is maintained at the top of this document.)*
