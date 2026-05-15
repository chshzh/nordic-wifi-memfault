# Product Requirements Document - nordic-wifi-memfault

## Document Information

| Field | Value |
|---|---|
| Product Name | nordic-wifi-memfault |
| Version | 2026-05-15-16-20 |
| Previous Version | 1.1 (legacy pm/PRD.md) |
| Status | Draft |
| Product Manager | Reverse update from implementation baseline |
| NCS Version | v3.3.0 workspace baseline |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse update: migrated canonical PRD to docs/pm-prd and reconciled requirements to currently implemented behavior |
| 2026-05-14-15-00 | Added FR-006: NTP time synchronization for real-world timestamps in debug log |
| 2026-05-15-16-20 | FR-006: extend acceptance criteria — NTP sync also gives Memfault dashboard events wall-clock timestamps (nrf54lm20dk only) |

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

- Settings storage is used for credential persistence.
- Dedicated crash storage partition is used for coredump retention.

### 2.4 Buttons and User Controls

| Input | Behavior |
|---|---|
| Button 1 short press | Trigger heartbeat and optional nRF70 CDR flow |
| Button 1 long press | Trigger stack-overflow demo fault path for validation |
| Button 2 short press | Trigger Memfault OTA check path |
| Button 2 long press | Trigger division-by-zero demo fault path |

### 2.5 Cloud and Monitoring

- Memfault integration for crash, metrics, and OTA.
- Runtime metrics include connectivity and memory health paths.

---

## 3. Functional Requirements

### P0 - Must Have

| ID | As a... | I want to... | So that... | Acceptance Criteria | Engineering Spec |
|---|---|---|---|---|---|
| FR-001 | user | power on and connect device to Wi-Fi using stored/provisioned credentials | cloud features can run | Device reaches connected state and logs network-ready flow | [network-module.md](../dev-specs/network-module.md), [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md) |
| FR-002 | user | have telemetry uploaded after connectivity is ready | I can monitor device health remotely | Connect event triggers Memfault upload/heartbeat path | [app-memfault-module.md](../dev-specs/app-memfault-module.md), [network-module.md](../dev-specs/network-module.md), [heap-monitor-module.md](../dev-specs/heap-monitor-module.md) |
| FR-003 | developer | use button 1 actions for validation | I can test heartbeat and crash reporting quickly | Short press triggers heartbeat path; long press triggers demo crash path | [button-module.md](../dev-specs/button-module.md), [app-memfault-module.md](../dev-specs/app-memfault-module.md) |
| FR-004 | developer | use button 2 actions for OTA/fault validation | I can validate update and fault handling flows | Short press triggers OTA check; long press triggers demo crash path | [button-module.md](../dev-specs/button-module.md), [app-memfault-module.md](../dev-specs/app-memfault-module.md) |

### P1 - Should Have

| ID | As a... | I want to... | So that... | Acceptance Criteria | Engineering Spec |
|---|---|---|---|---|---|
| FR-005 | developer | enable optional BLE/HTTPS/MQTT support paths | I can validate broader connectivity behavior | Optional modules start and react to WIFI_CHAN connectivity state | [app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md), [app-https-client-module.md](../dev-specs/app-https-client-module.md), [app-mqtt-client-module.md](../dev-specs/app-mqtt-client-module.md) |
| FR-006 | developer | have the device synchronize its clock from an NTP server after connecting to the network | debug log lines show real-world wall-clock timestamps instead of uptime-relative milliseconds, and Memfault events carry accurate timestamps on the dashboard | After network ready event, device queries pool.ntp.org; subsequent log lines display ISO date/time; Memfault events captured after first sync show wall-clock captured_date on the dashboard (nrf54lm20dk only); events before sync are marked unknown rather than epoch-0; feature is Kconfig-gated and off by default | [ntp-module.md](../dev-specs/ntp-module.md), [app-memfault-module.md](../dev-specs/app-memfault-module.md) |

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

| Board | Wi-Fi | Notes |
|---|---|---|
| nRF7002DK (nrf5340/cpuapp) | onboard nRF7002 | dual-core host scenario |
| nRF54LM20DK + nRF7002EB2 | shield-based nRF7002 | board-specific RRAM coredump and coexistence settings |

### 5.2 Board-Specific Notes

- nRF54LM20DK board config disables specific coexistence and spin-validate settings
  to match hardware/runtime constraints.
- Coredump backend differs by board due to flash controller differences.

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

---

## 10. Engineering Spec References

| Spec file | Covers |
|---|---|
| [../dev-specs/overview.md](../dev-specs/overview.md) | Spec index and requirement mapping |
| [../dev-specs/architecture.md](../dev-specs/architecture.md) | System architecture and channel flows |
| [../dev-specs/button-module.md](../dev-specs/button-module.md) | Button interaction behavior |
| [../dev-specs/network-module.md](../dev-specs/network-module.md) | Connectivity lifecycle |
| [../dev-specs/app-wifi-prov-ble-module.md](../dev-specs/app-wifi-prov-ble-module.md) | BLE provisioning flow |
| [../dev-specs/heap-monitor-module.md](../dev-specs/heap-monitor-module.md) | Heap telemetry and monitoring |
| [../dev-specs/app-memfault-module.md](../dev-specs/app-memfault-module.md) | Memfault integration behavior |
| [../dev-specs/app-https-client-module.md](../dev-specs/app-https-client-module.md) | HTTPS periodic checks |
| [../dev-specs/app-mqtt-client-module.md](../dev-specs/app-mqtt-client-module.md) | MQTT periodic checks |
