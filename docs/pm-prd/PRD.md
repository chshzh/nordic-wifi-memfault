# Product Requirements Document

## Document Information

| Field | Value |
|---|---|
| Product Name | nordic-wifi-memfault (Memfault Wi-Fi Observability Sample) |
| Version | 2026-08-20-14-56 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Status | Implemented |

> **Status values:** `Draft` → `In Review` → `Approved` → `Implemented` → `Archived`

> `Version` = the latest Changelog entry timestamp (the current edit time, `date +%Y-%m-%d-%H-%M`); bump it on **every** change. Keep these exact fields from creation through maintenance — no `Latest Version` variant.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-08-20-14-56 | **Simplified Wi-Fi reconnect retry to a flat interval, by request**: removed the capped exponential backoff added in 2026-07-24-11-30 (5 s ×3 quick retries, then 30→60→120→300 s cap). The quick 5 s first attempt after a disconnect is unchanged; every subsequent retry now uses a flat 180 s (3 min) interval instead of escalating. Simpler and easier to reason about; no field data suggested the escalating tiers were needed. See [network-module.md](../dev-specs/network-module.md) Changelog for the implementation detail. |
| 2026-08-20-14-47 | **Internal architecture refactor, no behavior/requirement change**: STA auto-reconnect (backoff retry + boot-time auto-connect using stored Wi-Fi credentials) moved from `wifi_prov_over_ble.c` into the always-compiled `network` module, so it no longer silently disappears if `CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED` is ever disabled in favor of `wifi cred shell`-only provisioning. A new `BLE_CHAN` Zbus channel lets `network` still avoid racing an active BLE provisioning session's own connect attempt, without depending on the BLE module at compile time. See [network-module.md](../dev-specs/network-module.md) and [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md) for detail. Build-verified clean on nRF7002DK (RAM 99.55%, unchanged within 8 B noise). |
| 2026-08-20-14-10 | **Removed all SoftAP scaffolding** — this sample only uses Wi-Fi STA mode. Deleted the dormant `network/Kconfig` SoftAP options (`SOFTAP_SSID`, `SOFTAP_PASSWORD`, `SOFTAP_CHANNEL`, `SOFTAP_BAND_*`, `SOFTAP_REG_DOMAIN`), `net_event_mgmt.c`'s SoftAP event handlers/station tracking, and `wifi_utils.c`'s `wifi_run_softap_mode()`/`wifi_set_softap()`/`wifi_set_reg_domain()`/DHCP-server helpers and the unused `wifi_utils_ensure_gateway_softap_credentials()` — all gated behind `CONFIG_WIFI_NM_WPA_SUPPLICANT_AP`, which was never selected anywhere in this project, so none of it was ever compiled in (confirmed by an unchanged flash size after removal, within noise: 849560 B → 849576 B). Build-verified clean on nRF7002DK. |
| 2026-08-20-13-20 | **Zbus event redesign**: `WIFI_STA_CONNECTED` (misleadingly named — actually fired on IP assignment, not L2 association) and dead `WIFI_DNS_READY` removed from `wifi_msg_type`; connectivity-gated behavior (Memfault upload, OTA-on-connect, NTP sync, BLE status, HTTPS/MQTT clients) now driven by `NETWORK_CHAN`'s `NETWORK_READY`/`NETWORK_NOT_READY` instead. FR-002's acceptance criteria reworded to avoid naming an internal enum. See [network-module.md](../dev-specs/network-module.md) for detail. |
| 2026-02-06-00-00 | Initial draft (legacy `pm/PRD.md`, single-board nRF7002DK, wifi/button module split) |
| 2026-03-03-00-00 | Refactoring pass — SMF+Zbus modular architecture, HTTPS/MQTT clients always-on, WiFi provisioning over BLE |
| 2026-07-13-11-07 | Migrated from `pm/PRD.md` to `docs/pm-prd/PRD.md` (new template) and synced to current `src/` on the `ncs264` branch: dual-board support (nRF7002DK + nRF54LM20DK+nRF7002EB II), `wifi` module renamed/split into `network` module (net event mgmt + wifi utils), new `heap_monitor` module, port from NCS v3.2.4 → v2.6.4 (legacy Partition Manager, sysbuild `--sysbuild` flag required, underscore board targets) |
| 2026-07-13-12-22 | Added FR-102 and FR-103 (P1) — ported from `nordic-wifi-memfault-main`'s FR-007/FR-008: disconnect-time log-state and nRF70 CDR persistence across a power cycle, restored and uploaded to Memfault on the next Wi-Fi reconnect. Not yet implemented on this branch — Phase 2 spec + Phase 3 coding required. |
| 2026-07-13-13-31 | FR-102/FR-103 implemented and build-verified on nRF7002DK (FLASH 90.26%, RAM 98.75%). FR-102 uses a drain-and-replay approach (`memfault_log_read()` + `memfault_log_save_preformatted()`) instead of a raw ring-buffer copy, since `memfault_log_get_state()`/`memfault_log_restore_state()` do not exist in the Memfault SDK v1.6.0 bundled with NCS v2.6.4; replayed entries carry the restore-time timestamp rather than the original disconnect-time timestamp. FR-103 ported directly, unchanged behavior. |
| 2026-07-24-11-30 | Reliability hardening pass, found while evaluating FR-102 in the field: (1) **FR-102 correctness fix** — the disconnect-time log-state drain used to keep the *oldest* unread entries and silently drop whatever didn't fit in the 4 KB scratch buffer, so the log line closest to the actual disconnect (e.g. the Wi-Fi disconnect-reason line) could be missing from the uploaded blob whenever there was backlog (e.g. from failed Memfault chunk uploads). Now evicts oldest-first so the *newest* entries always survive — see updated FR-102 acceptance criteria. (2) **Wi-Fi reconnect reliability** — replaced the flat 180 s reconnect retry with a capped exponential backoff (5 s ×3, then 30→60→120→300 s), and added an L3 DHCP-bound watchdog (30 s default) that forces a clean reconnect if the device associates but never obtains an IP, or loses its lease while still linked — previously an unrecoverable state requiring a manual reset. (3) Ported four DNS-reliability fixes from `nordic-wifi-memfault-main` (concurrent-query collision, DHCP-to-resolver propagation delay, DHCP-renewal query cancellation retry, OTA/upload resolver contention stagger) that reduce the `DNS lookup ... failed: -11` errors observed during Memfault chunk uploads. (4) Fixed a Zephyr network-stack stack overflow (`CONFIG_NET_TCP_WORKQ_STACK_SIZE` default of 1024 B) that could crash the device (`USAGE FAULT`) when HTTPS and MQTT both resumed traffic immediately after a reconnect — diagnosed via a symbolicated Memfault coredump trace. |
| 2026-07-24-14-09 | Added FR-104 (P1) — NTP time synchronization, ported from `zego/bricks/ntp`: on Wi-Fi connect, queries an SNTP server and sets the system's real-world clock. This app has no other real-time source (`CONFIG_DATE_TIME`/`CONFIG_RTC`), so Memfault events and logs previously only ever got a server ingest-time timestamp; once synced, they now carry a real device UTC timestamp instead. UART log line timestamps are unaffected (this NCS/Zephyr version has no built-in real-time log-timestamp formatting). Also found during the FR-102 field investigation: Memfault batches all unsent logs under one shared trigger-time timestamp (not a per-line timestamp), so a restored disconnect-time batch still carries the *restore*-time timestamp, not each line's exact original time — FR-104 only upgrades that shared timestamp from "none" to "real UTC", it does not add per-line historical accuracy. Amended FR-102's acceptance criteria to reflect this precisely. |
| 2026-07-24-14-41 | Two FR-104 corrections found while hardware-testing: (1) the initial build never actually ran — `src/modules/ntp/CMakeLists.txt` used the `zego` brick's `zephyr_library()` pattern, which this app's plain (non-west-module) build never links in; switched to `target_sources(app PRIVATE ...)`, this app's convention for every other module, confirmed via UART log (`ntp_module: NTP sync initialized` / `Querying pool.ntp.org ...` now appear). (2) UART log timestamps *can* be switched to real time after all — `CONFIG_LOG_TIMESTAMP_USE_REALTIME` doesn't exist in this Zephyr version, but its logging core exposes `log_set_timestamp_func()` to swap the timestamp source at runtime; the module now registers a `CLOCK_REALTIME`-backed one once synced. Because this project uses 32-bit log timestamps, the value is seconds (not milliseconds) to avoid overflow, rendered via `CONFIG_LOG_OUTPUT_FORMAT_LINUX_TIMESTAMP=y` as `[<epoch_seconds>.000000]` instead of the uptime `[hh:mm:ss,ms,us]` format — a real, monotonically-correct Unix timestamp, not a calendar date string. |
| 2026-07-24-14-47 | Upgraded UART log timestamps from raw epoch seconds to a human-readable calendar UTC string: registered a custom Zephyr log formatter (`CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP=y`, replacing `CONFIG_LOG_OUTPUT_FORMAT_LINUX_TIMESTAMP`) that renders the synced time via `gmtime_r()` as `"2026-07-24 14:35:33Z"` instead of `"1784896533.000000"`; before sync, the same formatter shows an elapsed `hh:mm:ss.mmm` duration. |
| 2026-07-24-14-56 | **Bug fix**: hardware testing showed a handful of log lines briefly rendering bogus `1970-01-29` dates right around the sync transition, because the formatter checked the *live* sync flag at print time while `CONFIG_LOG_MODE_DEFERRED` formats messages asynchronously after capture. Fixed by tagging the epoch/uptime mode into the raw timestamp value itself at capture time. Also wrapped timestamps in `[...] ` brackets to match the original log style. |
| 2026-07-28-08-15 | **Bug fix**: the `tcp_work` stack overflow (first patched 2026-07-24 by bumping `CONFIG_NET_TCP_WORKQ_STACK_SIZE` 1024→2048) recurred in the field — 7 crashes on `v2.6.4.1` over 2026-07-27/28, found via Memfault reboot history + the same (still-open) symbolicated crash issue, now faulting within ~104 B of the *2048* top instead of the *1024* top. Bumped again to 4096; RAM headroom unaffected (72.74% used, same as before). Root cause of the growing stack usage not fully isolated — see `docs/dev-specs/3-memopt.md` Open Issues. |
| 2026-08-19-15-00 | **Wi-Fi reconnect reliability, round 2**: found via UART log analysis after a router power-cycle test — the device could still hang offline indefinitely even with the 2026-07-24-11-30 backoff/watchdog fix in place. Two independent bugs in `wifi_prov_over_ble.c`'s reconnect logic, both now fixed (see [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md) for detail): (1) a disconnect reported as "intentional" (`status == 0`) was always deferred to the BLE provisioner to reconnect, but the L3 DHCP-bound watchdog's own forced disconnect also reports `status == 0` — with no BLE provisioning session active, nothing ever reconnected. (2) a failed connection *attempt* (e.g. an association timeout mid-retry) was mistaken for a successful connection, cancelling the in-progress reconnect backoff and leaving the device offline with no further retries. **New feature**: periodic nRF70 CDR collection (`CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC`, off by default) — a fresh Wi-Fi firmware-stats snapshot is kept ready on a timer instead of only being collected on a Button-1 press or a disconnect event, so whichever CDR upload the Memfault cloud accepts (capped at 1/device/24h) reflects more recent link conditions. A companion periodic coredump-retry feature was prototyped alongside it but removed after it was found to be both redundant (a pending coredump is already retried automatically by Memfault's own periodic upload timer) and the direct cause of a boot-time out-of-RAM assertion loop on nRF7002DK — see `app-memfault-module.md` Changelog for the full RAM postmortem. Also tightened `CONFIG_MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS`, `CONFIG_APP_MEMFAULT_HEARTBEAT_FORCE_INTERVAL_SEC`, `CONFIG_APP_HTTPS_REQUEST_INTERVAL_SEC`, and `CONFIG_APP_MQTT_CLIENT_PUBLISH_INTERVAL_SEC` from 900 s to 60 s for faster feedback during active development/testing. |
| 2026-08-20-11-25 | Removed the `nRF7120DK` bullet from §8 Out of Scope. |
| 2026-08-20-10-59 | Removed P2 (FR-201, SoftAP/Wi-Fi Direct) — this project is nRF7002DK-only, and SoftAP/P2P were already covered as unimplemented scaffolding in §8 Out of Scope, so a separate "Nice to Have" tier was redundant. **Corrected Buttons & LEDs hardware facts**: nRF7002DK physically has only 2 buttons and 2 LEDs (confirmed against `nrf5340_cpuapp_common.dts`), not the previously documented "4 available" buttons. Removed the Button 3/Button 4 rows from the button behavior table and noted that the button module's `DK_BTN3_MSK`/`DK_BTN4_MSK` tracking and `app_memfault`'s button-3/4 metric/trace handlers are dead code on this board since those buttons don't exist. Also fixed the stale "Buttons 1–4" reference in §6.2 Normal Operation to "Buttons 1–2". |
| 2026-08-19-15-45 | **Removed nRF54LM20DK + nRF7002EB II support entirely.** This board's SoC (nRF54LM20A) has no board definition anywhere in this NCS v2.6.4 installation (confirmed absent from `zephyr/boards`, `nrf/boards`, `modules/hal/nordic`) — the nRF54L series was introduced in a later NCS release than the one this `ncs264` branch targets. This project's `boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`/`.overlay` files only ever overrode Kconfig/DTS on top of a base board definition that was never actually present, which is exactly the persistent, previously-unresolved "BOARD_ROOT environment issue" this doc's engineering specs have flagged as an Open Issue since 2026-07-13 — the board build was never actually verified working in this environment, and never could be. **Retired FR-006** (dual-board support — see §3 P0 table, struck through, ID reserved rather than renumbered) and updated every other board-comparison table/note in this document (§1.3, §1.5 A3 dropped, §2.4, §5, §7) to reflect nRF7002DK as the sole target. Deleted board/partition/sysbuild files and updated CI/README/dev-specs — see [1-architecture.md](../dev-specs/1-architecture.md) Changelog for the full technical account. |

---

## 1. Executive Summary

### 1.1 Product Overview

`nordic-wifi-memfault` is a Memfault integration reference sample for Nordic Wi-Fi platforms. It demonstrates IoT device observability — Wi-Fi STA connectivity, Wi-Fi credential provisioning over BLE, always-on HTTPS/MQTT client traffic, and Memfault cloud-based crash reporting, metrics, and OTA firmware updates — on the nRF7002DK (nRF5340 + nRF7002).

### 1.2 Problem Statement

Developers integrating Memfault with Nordic Wi-Fi hardware need a working, current reference showing the SMF+Zbus modular architecture pattern, correct Memfault SDK wiring (coredump, metrics, OTA), and Wi-Fi lifecycle handling — without reverse-engineering scattered examples.

### 1.3 Target Users

| User type | Description |
|---|---|
| Primary | Embedded developers integrating Memfault with nRF70 Wi-Fi devices |
| Secondary | QA and support teams validating connectivity and OTA flows |
| Tertiary | Nordic field engineers running demos on nRF7002DK |

### 1.4 Success Metrics

| Metric | Target | How to measure | Verified by |
|---|---|---|---|
| Clean build | Zero compiler errors | `west build -p --sysbuild` | **chsh-sk-ncs-4.1-verification** — build verification step |
| Wi-Fi connects | STA connects and gets an IP within 30 s of credentials being available | UART log timestamp (`WiFi CONNECTED`) | **chsh-sk-ncs-4.2-validation** — hardware runtime scenario |
| Memfault upload | Heartbeat/coredump data reaches the Memfault dashboard after connect | Manual dashboard check | **chsh-sk-ncs-4.2-validation** — hardware runtime scenario |
| OTA | Firmware update via Memfault FOTA completes and device reboots into new image | Manual OTA test | **chsh-sk-ncs-4.2-validation** — hardware runtime scenario |
| No credential leakage | Wi-Fi password never appears in UART logs or git history | Log/code review | **chsh-sk-ncs-4.1-verification** — build verification step |

### 1.5 Assumptions

| # | Assumption | Risk if wrong |
|---|---|---|
| A1 | Target network is 2.4 GHz WPA2/WPA3 infrastructure Wi-Fi | High — STA connect flow is the only supported mode |
| A2 | Developer has a Memfault project and API key | High — no cloud features work without it |
| A3 | Demo environment allows outbound HTTPS/MQTT (443/8883) to `example.com`, `broker.emqx.io`, and Memfault | Low — HTTPS/MQTT client metrics will show failures but core Memfault flow still works |

---

## 2. Device Capabilities

### 2.1 Wi-Fi Connectivity

- [x] **Connect to an existing Wi-Fi network (STA mode)** — device joins a home or office network like a laptop would
- [ ] **Create its own Wi-Fi hotspot (SoftAP mode)** — not implemented; this sample is STA-only — see Out of Scope
- [ ] **Connect directly to a phone without a router (P2P / Wi-Fi Direct)** — not implemented

*Notes: STA is the only active mode. There is no runtime mode switch. Wi-Fi credentials are entered once via BLE provisioning (or manually via NVS-backed `wifi_credentials`) and persist across reboots.*

### 2.2 Communication & Protocols

- [ ] **Web interface** — not implemented
- [ ] **REST API** — not implemented
- [x] **MQTT messaging** — always-on TLS-secured echo test client to `broker.emqx.io`
- [ ] **CoAP** — not implemented
- [ ] **Reachable by name** — not implemented (device is a Wi-Fi client, not a discoverable host)
- [x] **HTTPS client** — always-on periodic `HEAD` requests to `example.com` (connectivity health check + metrics, not a user-facing web/API feature)

### 2.3 Storage & Memory

- [x] **Remember settings after power-off** — MCUboot image confirmation state, Memfault coredump partition
- [x] **Remember Wi-Fi credentials** — stored via Zephyr `wifi_credentials` (NVS-backed `settings_storage` partition); device reconnects automatically after reboot

### 2.4 Buttons & LEDs

| Hardware | nRF7002DK |
|---|---|
| Buttons used | 2 (of 2 available) — Button 1, Button 2 (`DK_BTN1`/`DK_BTN2`) |
| LEDs used | 0 (of 2 available) — no LED module yet |

> nRF7002DK physically has only 2 buttons and 2 LEDs. The button module's `BUTTON_COUNT` is 4 and it also tracks `DK_BTN3_MSK`/`DK_BTN4_MSK`, and `app_memfault` has handlers for button numbers 3 and 4 (metric increment / trace event demos) — but since buttons 3/4 don't exist on this board, those masks never change and that code path is unreachable on nRF7002DK.

*Button behavior:*

| Button | Press | Action |
|---|---|---|
| Button 1 / BUTTON 0 | Short (< 3 s) | Trigger Memfault heartbeat + nRF70 firmware-stats CDR upload |
| Button 1 / BUTTON 0 | Long (≥ 3 s) | Stack overflow crash demo (recursive Fibonacci) — tests coredump capture |
| Button 2 / BUTTON 1 | Short (< 3 s) | Trigger Memfault OTA check |
| Button 2 / BUTTON 1 | Long (≥ 3 s) | Division-by-zero crash demo — tests fault handler |

*LED behavior:* Not implemented in this release (no LED module).

### 2.5 Cloud & Monitoring

- [x] **Memfault** — crash reporting (coredump), metrics/heartbeats, trace events, OTA firmware updates, nRF70 Wi-Fi firmware-stats Custom Data Recording (CDR)
- [x] **BLE credential provisioning** — Wi-Fi credentials set via the nRF Wi-Fi Provisioner mobile app (Android/iOS) over BLE

### 2.6 Developer & Debug Features

- [ ] **Serial shell** — disabled by default (`CONFIG_SHELL=n`) to save flash; can be re-enabled for debugging
- [x] **Verbose startup log** — board name, firmware version, build date/time, MAC address, and enabled-module list printed at boot
- [x] **Real-world Memfault timestamps** — device clock synced via NTP once connected, so Memfault events/logs and UART log lines show a real Unix-epoch timestamp instead of only a server ingest-time or device-uptime value (FR-104; UART is whole-second epoch, not a calendar-date string)

---

## 3. Functional Requirements

### P0 — Must Have

| ID | As a… | I want to… | So that… | Acceptance Criteria | Engineering Spec |
|---|---|---|---|---|---|
| FR-001 | developer | power on the device and have it connect to Wi‑Fi using stored or newly-provisioned credentials | I don't have to hardcode credentials or use a shell | - No credentials stored → device advertises as `PV<MAC>` for BLE provisioning<br>- Credentials present → device connects within 30 s of boot<br>- `WiFi CONNECTED` logged over UART with IP address | [network-module.md](../dev-specs/network-module.md), [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md) |
| FR-002 | developer | have crash and metrics data automatically uploaded to Memfault once connected | I can monitor fleet health without manual steps | - Once the network is ready (IP address assigned), device waits for DNS then uploads any queued data<br>- Coredump from a crash appears on the Memfault dashboard after next boot + connect<br>- Heartbeat metrics appear in the dashboard | [app-memfault-module.md](../dev-specs/app-memfault-module.md) |
| FR-003 | developer | press Button 1 to trigger a heartbeat and, held long, a stack-overflow demo | I can test the metrics and crash-reporting pipeline on demand | - Short press logs "Memfault heartbeat" and posts data if Wi-Fi connected<br>- Long press (≥3s) crashes via stack overflow and a coredump is captured | [button-module.md](../dev-specs/button-module.md), [app-memfault-module.md](../dev-specs/app-memfault-module.md) |
| FR-004 | developer | press Button 2 to check for an OTA update and, held long, trigger a division-by-zero demo | I can test the OTA and fault-handling pipeline on demand | - Short press starts `memfault_fota_start()` and logs the result<br>- Long press (≥3s) crashes via division-by-zero and a coredump is captured<br>- OTA is also auto-checked on Wi-Fi connect and every `CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN` minutes | [button-module.md](../dev-specs/button-module.md), [app-memfault-module.md](../dev-specs/app-memfault-module.md) |
| FR-005 | developer | have HTTPS and MQTT client traffic running automatically once Wi-Fi connects | I can validate general network connectivity and see it reflected in Memfault metrics | - HTTPS client sends a `HEAD` request to `example.com` every `CONFIG_APP_HTTPS_REQUEST_INTERVAL_SEC` (default 300 s)<br>- MQTT client publishes/echoes a message to `broker.emqx.io` every `CONFIG_APP_MQTT_CLIENT_PUBLISH_INTERVAL_SEC` (default 300 s)<br>- Success/failure counters visible as Memfault metrics | [app-https-client-module.md](../dev-specs/app-https-client-module.md), [app-mqtt-client-module.md](../dev-specs/app-mqtt-client-module.md) |
| ~~FR-006~~ | ~~developer~~ | ~~run the same application on nRF7002DK or nRF54LM20DK+nRF7002EB II~~ | ~~I can validate the reference design across Nordic's current Wi-Fi DK lineup~~ | **Removed 2026-08-19** — nRF54LM20DK has no board definition in NCS v2.6.4; project is nRF7002DK-only. ID reserved, not reused. | [1-architecture.md](../dev-specs/1-architecture.md) |
| FR-007 | developer | see live heap usage feed into Memfault metrics | I can catch heap exhaustion before it causes a crash | - System heap and mbedTLS heap usage logged periodically (`CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC`)<br>- A warning is logged when usage crosses `CONFIG_HEAPS_MONITOR_WARN_PCT`<br>- Heap metrics visible in Memfault heartbeat | [heap-monitor-module.md](../dev-specs/heap-monitor-module.md) |

### P1 — Should Have

| ID | As a… | I want to… | So that… | Acceptance Criteria | Engineering Spec |
|---|---|---|---|---|---|
| FR-101 | developer | have nRF70 Wi-Fi firmware PHY/LMAC/UMAC statistics uploaded to Memfault as CDR | I can diagnose Wi-Fi link-quality issues remotely | - Triggered by Button 1 short press (bundled with heartbeat)<br>- Limited to 1 upload per device per 24 h (Memfault CDR limit)<br>- Data parseable with `script/nrf70_fw_stats_parser.py` | [app-memfault-module.md](../dev-specs/app-memfault-module.md) |
| FR-102 | developer | have disconnect-time log diagnostics survive a power cycle and appear in the Memfault platform after the device reconnects | I can root-cause field connectivity failures without physical access to the device | - On Wi-Fi disconnect / network-not-ready, the current Memfault ring-buffer log state is saved exactly once to a dedicated partition on the external SPI NOR flash (persist-once guard prevents duplicate saves when multiple network layers fire)<br>- If the unread backlog exceeds the 4 KB scratch buffer (e.g. because Memfault uploads have been failing), the **newest** entries are kept and the oldest are evicted, so the log lines closest to the disconnect (e.g. the disconnect-reason line) always survive<br>- On the next Wi-Fi reconnect, the saved state is restored into the live Memfault log ring buffer and uploaded to the dashboard, then erased from external flash<br>- A visible separator line marks the boundary between restored and live log content in the Memfault cloud log view<br>- If the saved blob size doesn't match the live ring-buffer size (e.g. after a firmware update), it is discarded with no crash<br>- Restored entries are timestamped as a batch at restore time (Memfault applies one shared trigger-time timestamp to all unsent logs, not a per-line timestamp); without FR-104 this app has no other real-time source, so that timestamp was previously absent entirely (server ingest-time fallback only) — with FR-104 (NTP sync) enabled and synced, it becomes a real UTC restore-time timestamp instead, though still not each line's exact original time<br>- Feature is Kconfig-gated (`CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`, default `y`); ported from `nordic-wifi-memfault-main` FR-007 | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [2-pm-partition.md](../dev-specs/2-pm-partition.md) |
| FR-103 | developer | have disconnect-time nRF70 Wi-Fi firmware statistics (CDR) survive a power cycle and be uploaded to Memfault after the device reconnects | I can diagnose Wi-Fi radio/LMAC/UMAC state at the moment of disconnection without physical access to the device | - A short time after Wi-Fi disconnect / network-not-ready, firmware collects fresh nRF70 firmware statistics and saves the raw CDR blob to a dedicated partition on the external SPI NOR flash<br>- On the next Wi-Fi reconnect, the blob is restored so the existing CDR upload path (FR-101) picks it up and uploads it to Memfault, then the partition is erased<br>- Oversized blobs are discarded with a warning, no crash<br>- Feature is Kconfig-gated (`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`, default `y`), depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED`; ported from `nordic-wifi-memfault-main` FR-008 | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [2-pm-partition.md](../dev-specs/2-pm-partition.md) |
| FR-104 | developer | have the device's clock synchronized to real-world time once connected to Wi-Fi | I can correlate Memfault events and logs with wall-clock time instead of device uptime | - On Wi-Fi connect, the device queries an SNTP server (`CONFIG_NTP_MODULE_SERVER`, default `pool.ntp.org`) and sets the system real-time clock<br>- Failed queries are retried automatically; a successful sync is periodically refreshed to correct clock drift<br>- Reconnecting after a disconnect triggers a fresh sync<br>- Once synced, Memfault events and logs carry a real UTC timestamp instead of only a server ingest-time timestamp (this app has no other real-time source configured)<br>- UART log line timestamps switch from an elapsed `[hh:mm:ss.mmm] ` duration to a calendar UTC string (e.g. `[2026-07-24 14:35:33Z] `) once synced<br>- Feature is Kconfig-gated (`CONFIG_NTP_MODULE_ENABLED`, default `y`); ported from `zego/bricks/ntp` | [ntp-module.md](../dev-specs/ntp-module.md) |

---

## 4. Non-Functional Requirements

### 4.1 Performance

| Behaviour | Target |
|---|---|
| Boot to Wi-Fi connect attempt | Immediate (no artificial boot delay in current `network` module) |
| Button press → published Zbus event | < 200 ms (state machine is interrupt-driven) |
| Wi-Fi reconnect after brief outage | Automatic — quick 5 s first retry after disconnect, then a flat 180 s (3 min) retry interval; no hardware-validated SLA measured yet |

### 4.2 Reliability

| Expectation | Target |
|---|---|
| Continuous operation without restart | 24 hours (not yet validated on hardware — see [VALIDATION_PLAN.md](../qa-test/VALIDATION_PLAN.md) once created) |
| Automatic recovery after Wi-Fi drops | Yes — `network` module reacts to `NET_EVENT_WIFI_DISCONNECT_RESULT` / L4 disconnect and re-associates; an L3 DHCP-bound watchdog (30 s default, `CONFIG_WIFI_MODULE_STA_DHCP_TIMEOUT_SEC`) also forces a reconnect if the device associates but never obtains an IP, or loses its lease while still linked |
| Automatic recovery after power cycle | Yes — stored credentials auto-reconnect; MCUboot confirms/rolls back OTA images |

### 4.3 Security

| Expectation | Requirement |
|---|---|
| Wi-Fi credentials | Stored only in NVS (`settings_storage` partition) via `wifi_credentials`; never logged or hardcoded |
| Memfault project key | Provided via git-ignored `overlay-app-memfault-project-info.conf`, never committed |
| TLS | HTTPS/MQTT clients use `CONFIG_NET_SOCKETS_SOCKOPT_TLS`; MQTT uses a provisioned TLS credential tag (`CONFIG_MQTT_HELPER_SEC_TAG`) |

---

## 5. Hardware

### 5.1 Target Development Kits

| Board | Wi-Fi chip | Buttons used | LEDs used | Supported modes |
|---|---|---|---|---|
| nRF7002DK | nRF7002 (on-board) | 2 (of 2 available) | 0 (of 2 available) | STA |

> **nRF54LM20DK + nRF7002EB II support removed 2026-08-19** — that board has no board definition
> in NCS v2.6.4 (the nRF54L series was introduced in a later NCS release). See
> [1-architecture.md](../dev-specs/1-architecture.md) Changelog for the full removal.

### 5.2 Board-specific notes

- nRF7002DK: nRF5340 dual-core; BLE runs on the network core (`hci_ipc`); UART on VCOM1.
- Uses MCUboot with an external SPI NOR (`MX25R64`) secondary OTA slot.
- This `ncs264` branch targets **NCS v2.6.4**, which uses legacy Partition Manager (`pm_static_<board>.yml`) — not the DTS-based partitioning used on NCS v3.3+. Board targets use the legacy underscore format (`nrf7002dk_nrf5340_cpuapp`) and require the explicit `--sysbuild` flag.

---

## 6. User Experience

### 6.1 First-time Setup

1. Copy `overlay-app-memfault-project-info.conf.template` to `overlay-app-memfault-project-info.conf` and set the Memfault project key.
2. Build and flash for the target board (see README.md Quick Start).
3. On first boot with no stored credentials, the device advertises as `PV<MAC>` over BLE.
4. Open the nRF Wi-Fi Provisioner app, connect to the device, select the Wi-Fi network, and enter the password.
5. Device connects, stores credentials in NVS, and begins uploading to Memfault.

### 6.2 Normal Operation

Device boots, reconnects automatically using stored Wi-Fi credentials, uploads any queued Memfault data, and runs the always-on HTTPS/MQTT clients and heap monitor in the background. Buttons 1–2 provide on-demand heartbeat/OTA/crash-demo actions.

### 6.3 Mode Selection

Not applicable — STA is the only mode; there is no runtime mode-selection menu.

### 6.4 Troubleshooting (known scenarios)

| Symptom | What the user should do |
|---|---|
| Device doesn't advertise for BLE provisioning | Confirm `CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED=y`; power-cycle if credentials already exist (it will skip advertising) |
| Build fails with `nanopb_pb2.py` / protobuf error | `export PYTHONNOUSERSITE=1` before building (user-site protobuf shadows toolchain's bundled nanopb generator) |
| MCUboot hangs with zero UART output after flashing | Confirm `--sysbuild` was passed at build time; NCS v2.6.4 does not default to sysbuild |
| No Memfault data on dashboard | Confirm project key set in `overlay-app-memfault-project-info.conf`; check DNS reaches `chunks-nrf.memfault.com` |

---

## 7. Release Criteria

- [x] All P0 FR acceptance criteria pass on nRF7002DK
- [ ] Device runs for 24 hours without restart (not yet validated — pending Phase 4.2)
- [x] Wi-Fi reconnects automatically after outage
- [x] No credentials visible in UART logs
- [x] README Quick Start guide covers build/flash commands

---

## 8. Out of Scope

- **SoftAP mode** — not implemented; this sample only uses Wi-Fi STA mode. The previously-existing SoftAP Kconfig scaffolding and `net_event_mgmt.c`/`wifi_utils.c` event handlers (dormant, gated behind `CONFIG_WIFI_NM_WPA_SUPPLICANT_AP` which was never selected) have been removed entirely.
- **Wi-Fi Direct / P2P mode** — not implemented.
- **LED status indication** — no LED module; `CONFIG_LED_MODULE_ENABLED` referenced in comments only.
- **Web interface / REST API** — device is a Wi-Fi client only, not a server.
- **Wi-Fi credential entry via shell** — `CONFIG_WIFI_STA_CRED_SHELL_ENABLED` referenced in comments only; shell is disabled by default.
