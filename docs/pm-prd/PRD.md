# Product Requirements Document - nordic-wifi-memfault

## Document Information

| Field | Value |
|---|---|
| Product Name | nordic-wifi-memfault |
| Version | 2026-06-22-12-40 |
| Status | Implemented |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse update: migrated canonical PRD to docs/pm-prd and reconciled requirements to currently implemented behavior |
| 2026-05-14-15-00 | Added FR-006: NTP time synchronization for real-world timestamps in debug log |
| 2026-05-15-16-20 | FR-006: extend acceptance criteria — NTP sync also gives Memfault dashboard events wall-clock timestamps (nrf54lm20dk only) |
| 2026-05-16-13-00 | FR-006: add periodic re-sync AC (every 6 h, 40 ppm drift, max 0.86 s error); Memfault OTA interval 30 min (48/day); HTTPS/MQTT per-request logs demoted to DBG, new INF summary logs added |
| 2026-05-16-17-00 | Memfault OTA check interval increased to 60 min (24/day); HTTPS request and MQTT publish intervals changed from 300 s to 900 s |
| 2026-05-19-09-07 | Added FR-007: persist disconnect-time log state to flash and restore it on next WiFi reconnect for Memfault cloud upload |
| 2026-05-20-14-00 | FR-007 revised: removed RAM-freeze (trigger_collection on disconnect); storage migrated to external flash partition; persist-once guard; both boards enabled; visual restore boundary in cloud logs |
| 2026-05-21-10-01 | Added FR-008: persist disconnect-time nRF70 CDR (WiFi firmware statistics) to external flash on disconnect; restore and upload to Memfault on next WiFi reconnect |
| 2026-05-22-10-00 | FR-001 behavior clarification: when the first stored AP is unavailable, the retry loop cycles through all stored credentials in sequence (round-robin) until one connects; retry plan with schedule and provisioner reminder is logged at loop start |
| 2026-06-04-22-00 | Added FR-009: LED Wi-Fi state feedback (nRF54LM20DK only). Button gestures (FR-003/FR-004) scoped to nRF54LM20DK only — nRF7002DK omits LED/button modules to meet flash budget. Updated hardware table and board-specific notes. |
| 2026-06-04-23-04 | Formatted Document Information (removed non-template fields; simplified NCS Version). FR-003/FR-004 Engineering Spec: replaced inline zego/button note with GitHub spec link. Removed Section 10 (Engineering Spec References — PRD does not own spec files). |
| 2026-06-22-12-40 | FR-007: mflt-log-state partition 8→12 KB (ring-buffer now 8 KB; blob overflows old 8 KB partition); fix spec links partition-layout.md→2-dts-partition.md (FR-007/FR-008) and ux.md→4-ux.md (FR-009) |
| 2026-06-19-12-31 | FR-002: updated Engineering Spec link from heap-monitor-module.md to memonitor-module.md (heap_monitor module replaced by zego/memonitor brick). |

---

## 1. Executive Summary

### 1.1 Product Overview

nordic-wifi-memfault is a Nordic Wi-Fi reference application demonstrating
Memfault observability on Wi-Fi enabled boards. The product connects to Wi-Fi,
collects runtime metrics and crash data, supports OTA checks through Memfault,
and provides BLE-based Wi-Fi onboarding with optional HTTPS and MQTT activity
modules for connectivity validation.

### 1.2 Problem Statement

Teams integrating Memfault with Nordic Wi-Fi devices need a working baseline that
shows secure connectivity, event-driven instrumentation, and field-update flows.
This product provides a reusable reference to reduce integration risk and shorten
bring-up time.

### 1.3 Target Users

| User type | Description |
|---|---|
| Primary | Embedded developers integrating Memfault in NCS Wi-Fi products |
| Secondary | QA and validation engineers verifying crash/OTA/telemetry behavior |
| Tertiary | Field/demo engineers demonstrating observability workflows |

### 1.4 Success Metrics

| Metric | Target | How to measure |
|---|---|---|
| Wi-Fi connection readiness | Device reaches L4 connected state after provisioning or stored credentials | UART log markers and channel event flow |
| Memfault data pipeline | Heartbeat/upload path triggered on connect and button events | UART logs and Memfault dashboard visibility |
| OTA trigger behavior | Manual and periodic OTA checks run without reboot loops | UART logs and stable runtime |
| Stability baseline | Application remains running without fatal faults during standard operation | Multi-hour runtime check |

### 1.5 Assumptions

| # | Assumption | Risk if wrong |
|---|---|---|
| A1 | User has valid Wi-Fi network and internet egress for Memfault endpoints | High |
| A2 | Memfault project key is provisioned via overlay file | High |
| A3 | Board-specific flash/rram partition setup matches current overlays | Medium |

---

## 2. Device Capabilities

### 2.1 Wi-Fi Connectivity

- Device operates in Wi-Fi STA connectivity model and publishes connection state.
- Credentials are persisted and reused on reboot.

### 2.2 Communication and Protocols

- BLE onboarding for Wi-Fi credentials (when enabled).
- HTTPS periodic check module (when enabled).
- MQTT periodic publish/echo module (when enabled).

### 2.3 Storage and Persistence

- Settings storage is used for credential persistence and compact disconnect-diagnostic blobs.
- Dedicated crash storage partition is used for coredump retention.

### 2.4 Buttons and User Controls

> **nRF54LM20DK + nRF7002EB2 only.** The nRF7002DK build omits the button and LED modules to stay within its 1 MB flash budget.

| Input | Behavior |
|---|---|
| Button 1 short press | Trigger heartbeat and optional nRF70 CDR collection |
| Button 1 long press | Trigger stack-overflow demo fault path for validation |
| Button 2 short press | Trigger Memfault OTA check |
| Button 2 long press | Trigger division-by-zero demo fault path |

### 2.5 LED State Feedback

> **nRF54LM20DK + nRF7002EB2 only.** LED 0 driven by `APP_WIFI_STATE_CHAN` via the UX module.

| State | LED 0 effect | Description |
|---|---|---|
| Boot / connecting | ROTATE (all LEDs) | Starts at boot; continues until connected |
| Connected (STA) | Solid ON | Wi-Fi link established |
| Disconnected / error | Fast BLINK (100 ms) | Attention needed |

### 2.6 Cloud and Monitoring

- Memfault integration for crash, metrics, and OTA.
- Runtime metrics include connectivity and memory health paths.

---

## 3. Functional Requirements

### P0 - Must Have

| ID | As a... | I want to... | So that... | Acceptance Criteria | Engineering Spec |
|---|---|---|---|---|---|
| FR-001 | user | power on and connect device to Wi-Fi using stored/provisioned credentials | cloud features can run | Device reaches connected state and logs network-ready flow; if the first stored AP times out, the retry loop cycles through all stored credentials in sequence (round-robin) until one connects; retry plan is logged at loop start with per-SSID scheduled times and a provisioner tip | [network-module.md](../dev-specs/network-module.md), [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md) |
| FR-002 | user | have telemetry uploaded after connectivity is ready | I can monitor device health remotely | Connect event triggers Memfault upload/heartbeat path | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [network-module.md](../dev-specs/network-module.md), [memonitor-module.md](../dev-specs/memonitor-module.md) |
| FR-003 | developer | use button 1 actions for validation | I can test heartbeat and crash reporting quickly | Short press triggers heartbeat path; long press triggers demo crash path **(nRF54LM20DK only — button module disabled on nRF7002DK)** | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [zego/button ↗](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) |
| FR-004 | developer | use button 2 actions for OTA/fault validation | I can validate update and fault handling flows | Short press triggers OTA check; long press triggers demo crash path **(nRF54LM20DK only — button module disabled on nRF7002DK)** | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [zego/button ↗](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) |

### P1 - Should Have

| ID | As a... | I want to... | So that... | Acceptance Criteria | Engineering Spec |
|---|---|---|---|---|---|
| FR-005 | developer | enable optional BLE/HTTPS/MQTT support paths | I can validate broader connectivity behavior | Optional modules start and react to WIFI_CHAN connectivity state | [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md), [app-https-client-module.md](../dev-specs/app-https-client-module.md), [app-mqtt-client-module.md](../dev-specs/app-mqtt-client-module.md) |
| FR-006 | developer | have the device synchronize its clock from an NTP server after connecting to the network | debug log lines show real-world wall-clock timestamps instead of uptime-relative milliseconds, and Memfault events carry accurate timestamps on the dashboard | After network ready event, device queries pool.ntp.org; subsequent log lines display ISO date/time; Memfault events captured after first sync show wall-clock captured_date on the dashboard (nrf54lm20dk only); events before sync are marked unknown rather than epoch-0; device re-syncs periodically every CONFIG_NTP_RESYNC_INTERVAL_SEC seconds (default 21600 = 6 h) to compensate for crystal drift (40 ppm, max 0.86 s error per interval); feature is Kconfig-gated and off by default | [ntp-module.md](../dev-specs/ntp-module.md), [app-memfault-module.md](../dev-specs/app-memfault-module.md) |
| FR-007 | developer | have disconnect-time log diagnostics survive a power cycle and appear in the Memfault platform after the device reconnects | I can root-cause connectivity failures that happen in the field without physical access to the device | On WIFI_STA_DISCONNECTED or NETWORK_NOT_READY: firmware saves the full Memfault ring-buffer state to a dedicated 12 KB partition on the external SPI/QSPI NOR flash exactly once per disconnect event (persist-once guard prevents duplicate persists when multiple network layers fire simultaneously); on next WiFi reconnect the persisted state is restored directly into the live Memfault ring buffer, `memfault_log_trigger_collection()` marks it for upload, and the data is uploaded to Memfault cloud; persisted blob is erased from external flash after restore; if restore payload size does not match the live ring-buffer size (firmware update between disconnect and reconnect), the blob is silently discarded with no crash; the restored log file contains original wall-clock timestamps; a visual separator line `=== [LOG RESTORE] pre-disconnect logs above \| live session below ===` appears in the Memfault cloud log view at the boundary between restored and live content; feature is Kconfig-gated (`CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`); enabled on both nRF54LM20DK and nRF7002DK | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [2-dts-partition.md](../dev-specs/2-dts-partition.md) |
| FR-008 | developer | have disconnect-time nRF70 WiFi firmware statistics (CDR) survive a power cycle and be uploaded to the Memfault platform after the device reconnects | I can diagnose WiFi radio and LMAC/UMAC state at the moment of disconnection without physical access to the device | When the log-persist work fires 10 seconds after WIFI_STA_DISCONNECTED or NETWORK_NOT_READY: firmware collects fresh nRF70 firmware statistics (PHY/LMAC/UMAC) and saves the raw CDR blob to a dedicated 8 KB partition on the external SPI/QSPI NOR flash (`mflt-cdr-state`); on next WiFi reconnect the blob is restored into the CDR source buffer so that the existing `has_cdr_cb` returns true and the next `memfault_zephyr_port_post_data()` call uploads the CDR file to Memfault cloud; the partition is erased after restore; if the blob size exceeds `NRF70_FW_STATS_BLOB_MAX_SIZE` it is discarded with a warning; feature is Kconfig-gated (`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`), depends on `CONFIG_NRF70_FW_STATS_CDR_ENABLED`; enabled on both nRF54LM20DK and nRF7002DK | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [2-dts-partition.md](../dev-specs/2-dts-partition.md) |
| FR-009 | developer | see Wi-Fi connection state on LED 0 at a glance | I can tell whether the device is connecting or connected without a UART terminal | Boot → ROTATE on all LEDs; STA connected → solid ON; disconnected/error → fast BLINK (100 ms half-period); `CONFIG_APP_UX_MODULE=y` required; **nRF54LM20DK only** — nRF7002DK omits LED module to meet 1 MB flash budget | [4-ux.md](../dev-specs/4-ux.md) |

---

## 4. Non-Functional Requirements

### 4.1 Reliability

| Expectation | Requirement |
|---|---|
| Connectivity recovery | System handles disconnects and retries through network stack/manager events |
| Crash capture path | Demo and real faults should generate retained coredump artifacts |
| Config persistence | Credential settings persist across power cycles |

### 4.2 Resource Constraints

| Expectation | Requirement |
|---|---|
| Stack safety | Thread stack sizing follows measured headroom tuning |
| Heap safety | System and mbedTLS heap usage is monitored and logged |

### 4.3 Security

| Expectation | Requirement |
|---|---|
| Secret handling | Memfault project key is not committed and must be supplied via overlay file |
| Secure transport | HTTPS and MQTT client paths use TLS |

---

## 5. Hardware

### 5.1 Target Boards

| Board | Wi-Fi | Buttons | LEDs | Notes |
|---|---|---|---|---|
| nRF54LM20DK + nRF7002EB2 | shield-based nRF7002 | 3 (BUTTON0–2) | 4 | Full LED/button UX enabled; RRAM coredump; BLE prov |
| nRF7002DK (nrf5340/cpuapp) | onboard nRF7002 | — | — | Flash budget too tight for LED/button modules |

### 5.2 Board-Specific Notes

- **nRF7002DK**: `CONFIG_ZEGO_BUTTON`, `CONFIG_ZEGO_LED`, and `CONFIG_APP_UX_MODULE` are disabled — the 1 MB application flash is fully consumed by the Memfault + Wi-Fi + TLS stack. Button-triggered features (FR-003, FR-004) are unavailable on this board.
- **nRF54LM20DK + nRF7002EB2**: Button and LED modules enabled; full UX feedback active. Board config disables specific coexistence and spin-validate settings to match hardware/runtime constraints.
- Coredump backend differs by board due to flash controller differences (SPI NOR on nRF7002DK, RRAM on nRF54LM20DK).

---

## 6. User Experience

### 6.1 First-Time Setup

1. Configure Memfault project key in overlay-app-memfault-project-info.conf.
2. Build and flash for target board.
3. Provision Wi-Fi credentials (BLE flow or pre-existing credentials).
4. Observe startup banner and module activation logs.

### 6.2 Normal Operation

- Device reconnects with persisted credentials.
- Connectivity transitions trigger telemetry and optional client behavior.
- Button actions are available for quick validation workflows.

### 6.3 Troubleshooting (high-level)

| Symptom | User action |
|---|---|
| Device does not connect | Re-provision credentials and verify AP/network availability |
| No cloud upload activity | Verify Memfault key, DNS/network egress, and project configuration |
| Optional HTTPS/MQTT inactive | Confirm module enable flags and connectivity state |

---

## 7. Release Criteria

- All P0 requirements pass on intended board target(s).
- Build completes with required overlay configuration.
- No regressions in connection and upload trigger flows.
- Documentation in docs/pm-prd and docs/dev-specs is synchronized.

---

## 8. Out of Scope

| Not building in this PRD revision | Why |
|---|---|
| New product features not present in current code | Reverse-update pass documents implemented behavior only |
| Runtime mode framework beyond current STA-based flow | Not implemented in this application |

---

## 9. Open Questions

| # | Question | Owner | Due |
|---|---|---|---|
| 1 | Should QA snapshots be migrated from legacy pm/QA.md into docs/qa-test timestamped files next? | Team | Next QA cycle |
| 2 | Should README/NCS version badges be updated as part of this documentation migration? | Team | Next docs cycle |


