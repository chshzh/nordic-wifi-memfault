# Nordic Wi-Fi Memfault

[![Build](https://github.com/chshzh/nordic-wifi-memfault/actions/workflows/build.yml/badge.svg)](https://github.com/chshzh/nordic-wifi-memfault/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/chshzh/nordic-wifi-memfault?label=Release&color=skyblue)](https://github.com/chshzh/nordic-wifi-memfault/releases/latest)

---

## Project Overview

### Introduction

Nordic Wi-Fi Memfault is a reference application for integrating Memfault observability
with Nordic Wi-Fi hardware. It demonstrates Wi-Fi connectivity, BLE provisioning,
Memfault metrics and crash reporting, and OTA-trigger flows in a modular NCS design.

The application is centered on STA connectivity and event-driven modules that publish
or subscribe through zbus channels.

### Supported hardware

| Board | Build target |
|-------|--------------|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` |
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` |

### Features

- Wi-Fi STA connection lifecycle with reconnect handling
- BLE Wi-Fi credential provisioning (nRF Wi-Fi Provisioner)
- Memfault heartbeat, metrics, coredump reporting, and OTA checks
- Button-driven validation paths (heartbeat/CDR, OTA check, crash demos)
- NTP time synchronization — syncs system clock from `pool.ntp.org` after network ready; log timestamps show real-world UTC time (e.g. `[2026-05-14 19:34:52.299,000]`)
- Optional HTTPS periodic test module
- Optional MQTT periodic pub/sub echo test module
- Heap monitoring for system and mbedTLS heaps
- Modular architecture based on SYS_INIT + zbus

### Target Users

- **Evaluator** - flash release firmware and validate logs/events quickly, follow the [Evaluator Quick Start](#evaluator-quick-start) guide.
- **Developer** - build from source and modify modules/config, see [Developer Info](#developer-info) for build setup and [Documentation](#documentation) for product requirements, architecture, and per-module specs.

---

## Evaluator Quick Start

### Step 1 - Flash the firmware

Download pre-built firmware from the [Latest release](https://github.com/chshzh/nordic-wifi-memfault/releases/latest) page and flash your board.

Pre-built firmware is only available for approved custom Memfault projects because
project keys are project-specific. If your project is not listed below, it is not
currently supported for evaluator pre-built access.

| Supported project | Release artifact prefix | Status |
|-------------------|-------------------------|--------|
| nordic-test | `nord_project_*` | Supported |
| terrfaforma | `terr_project_*` | Supported |

For evaluation access to additional projects, contact charlie.shao@nordicsemi.no
or local Nordic Sales team. Alternatively, follow the Developer path and configure
your own memfault project key to build the firmware.

### Step 2 - Provision Wi-Fi over Bluetooth LE

Provision Wi-Fi credentials with [nRF Wi-Fi Provisioner app](https://www.nordicsemi.com/Products/Development-tools/nRF-Wi-Fi-Provisioner).

### Step 3 - Verify runtime behavior

Monitor logs via UART:

| Board | Port | Baud |
|-------|------|------|
| nRF7002DK | VCOM1 (`/dev/tty.usbmodem*3`) | 115200 |
| nRF54LM20DK + nRF7002EB2 | VCOM0 (`/dev/tty.usbmodem*1`) | 115200 |

Use serial terminal at `115200` and verify:
- boot banner and enabled module list,
- Wi-Fi connect/network-ready logs,
- Memfault upload or heartbeat trigger logs.

Explore your device behavior in [Memfault Cloud](https://app.memfault.com/) after
the device connects and begins uploading data.


---

## Buttons

| Board | Button |  Press | Action |
|-------|--------|--------|--------|
| nRF7002DK | Button 1 | short(<3s) | Trigger heartbeat and optional nRF70 CDR path |
| nRF7002DK | Button 1 | long(>=3s) | Stack overflow demo crash path |
| nRF54LM20DK + nRF7002EBII | BUTTON0 | short(<3s) | Trigger OTA check path |
| nRF54LM20DK + nRF7002EBII | BUTTON1 | long(>=3s) | Division-by-zero demo crash path |

---

## Developer Info

### Project Structure

```text
nordic-wifi-memfault/
├── CMakeLists.txt
├── Kconfig
├── prj.conf
├── west.yml
├── docs/
│   ├── pm-prd/
│   │   └── PRD.md
│   ├── dev-specs/
│   │   ├── overview.md
│   │   ├── architecture.md
│   │   ├── flash-memory-layout.md
│   │   ├── button-module.md
│   │   ├── network-module.md
│   │   ├── app-wifi-prov-ble-module.md
│   │   ├── heap-monitor-module.md
│   │   ├── app-memfault-module.md
│   │   ├── app-https-client-module.md
│   │   └── app-mqtt-client-module.md
│   └── qa-test/
│       └── QA-*.md
├── boards/
├── src/
│   ├── main.c
│   └── modules/
│       ├── button/
│       ├── network/
│       ├── heap_monitor/
│       ├── wifi_prov_over_ble/
│       ├── app_memfault/
│       ├── app_https_client/
│       ├── app_mqtt_client/
│       ├── ntp/│       
│       └── messages.h
├── overlay-app-memfault-project-info.conf.template
└── .github/workflows/build.yml
```

### Workspace Setup

West workspace is driven by [west.yml](west.yml). Which contains the ncs version this application based on, for example, the following content means ncs v3.3.0.

```sh
    - name: sdk-nrf
      path: nrf
      revision: v3.3.0
      import: true
      remote: ncs
```

Use nRF Connect for VS Code or a shell initialized with the NCS toolchain.

#### Method 1 (Preferred) — Add to an existing NCS installation

If you already have a matching NCS version installed, reuse it directly — no re-downloading required.

```sh
cd /opt/nordic/ncs/<ncs-version>   # your existing NCS workspace root

git clone https://github.com/chshzh/nordic-wifi-memfault.git

# Switch the workspace manifest to nordic-wifi-memfault (one-time change)
west config manifest.path nordic-wifi-memfault

# Sync — NCS repos already present, only new project repos are cloned
west update
```

#### Method 2 — Fresh installation as a Workspace Application

##### Option A: nRF Connect for VS Code

Follow the [custom repository guide](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/extension_custom_repo.html).

##### Option B: CLI

```sh
west init -m https://github.com/chshzh/nordic-wifi-memfault --mr main <workspace-dir>
cd <workspace-dir>
west update
```

See the Nordic guide on [Workspace Application Setup](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/dev_model_and_contributions/adding_code.html#workflow_4_workspace_application_repository_recommended) for details.

### Build
Fill `overlay-app-memfault-project-info.conf` with your memfault project key and fw version.

```bash
# nRF7002DK
west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -- \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"

# nRF54LM20DK + nRF7002EB2
west build -p -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -- \
  -DSHIELD=nrf7002eb2 \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"
```

### Flash

**First-time flash** (erases all flash including NVS — Wi-Fi credentials will need to be re-provisioned):

```bash
# nRF7002DK
west flash -d build_nrf7002dk --erase

# nRF54LM20DK
west flash -d build_nrf54lm20dk --recover
```

**Subsequent updates** (preserves NVS — Wi-Fi credentials are retained, no re-provisioning needed):

```bash
# nRF7002DK
west flash -d build_nrf7002dk

# nRF54LM20DK
west flash -d build_nrf54lm20dk
```

### Developer Notes

#### General

- Board-specific Kconfig overrides are in `boards/*.conf` (both nRF54LM20DK and nRF7002DK).
- Coredump and partition behavior follows DTS fixed-partitions for NCS v3.3.0.
- nRF7002DK uses a custom flash coredump backend (`CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM=y`) in `src/modules/app_memfault/core/memfault_flash_coredump_storage.c` to bypass the upstream `PARTITION_MANAGER_ENABLED` dependency in `CONFIG_MEMFAULT_NCS_INTERNAL_FLASH_BACKED_COREDUMP`.

#### Storage Architecture

Only coredumps are written to non-volatile storage and survive a reset. All other Memfault data lives in RAM and must be uploaded before a power cycle.

| Data | Kconfig | Storage | Volatile? |
|------|---------|---------|-----------|
| Coredump (nRF54LM20DK) | `CONFIG_MEMFAULT_COREDUMP_STORAGE_RRAM=y` | RRAM partition `memfault_coredump_partition` (64 KB at `0x1d5000`) | No — survives power cycle |
| Coredump (nRF7002DK) | `CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM=y` | Flash partition `memfault_storage` (64 KB at `0xf0000`) | No — survives power cycle |
| Heartbeat / trace events | `CONFIG_MEMFAULT_EVENT_STORAGE_SIZE=4096` | RAM ring buffer | Yes — lost on hard reset |
| Log file | `CONFIG_MEMFAULT_LOGGING_RAM_SIZE=4096` | RAM circular buffer; disconnect-time snapshot persisted to external flash (`mflt-log-state`, 8 KB on mx25r64) when `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE=y` | Conditionally no — survives power cycle via external flash restore on next reconnect |
| CDR (nRF70 FW stats) | `CONFIG_NRF70_FW_STATS_CDR_ENABLED=y` | Static RAM buffer (up to 1 KB) | Yes — lost on reboot before upload |

#### Free-Tier Rate Limits

| Feature | Free-tier limit | Rate at current config | Controlling config |
|---|---|---|---|
| Heartbeats | 100/day | 96/day (15 min interval) | `MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS=900` |
| Log files | 150/day, 1000/7-day | 96/day (piggybacks on upload interval) | `MEMFAULT_HTTP_PERIODIC_UPLOAD_INTERVAL_SECS=900` |
| OTA checks | 100/day | 24/day (60 min interval) | `MEMFAULT_OTA_CHECK_INTERVAL_MIN=60` |
| CDR | 1/day | On-demand (button press) | `NRF70_FW_STATS_CDR_ENABLED=y` — no firmware throttle |
| Coredumps | 24/day | Crash-triggered | N/A |
| Trace events | 100/day | Button-triggered | N/A |
| Reboot events | 100/day, 1400/14-day | Reset-triggered | N/A |
| Sessions | 16/day | 0 (not used) | N/A |

#### NTP Clock and Timeline Timestamps

The device uses the Nordic `date_time` library (fed by SNTP) as the UTC time source for all Memfault timestamps. If NTP has not yet synced when an event is recorded, no device timestamp is embedded and the Memfault server falls back to the HTTP receive time for display.

At 40 ppm crystal drift the device re-syncs every 6 hours (`CONFIG_NTP_RESYNC_INTERVAL_SEC=21600`), keeping timestamp error ≤ 0.86 s. Adjust for tighter or looser requirements (3 h → ≤ 0.43 s, 12 h → ≤ 1.73 s).

| Data | When the timestamp is captured | What it means on the Memfault timeline | If NTP not yet synced |
|---|---|---|---|
| Log lines | When the log line is written to the Memfault RAM buffer — not at upload | UTC time the device emitted the log | Server uses HTTP receive time |
| Heartbeat metrics | When the 900 s window closes and `memfault_metrics_heartbeat_serialize` runs — not when individual metric values were set during the window | UTC time the measurement window ended | Server uses HTTP receive time |

## Documentation

Start with [docs/dev-specs/overview.md](docs/dev-specs/overview.md).

| Document | Description |
|---|---|
| [docs/pm-prd/PRD.md](docs/pm-prd/PRD.md) | Product requirements and acceptance criteria |
| [docs/dev-specs/overview.md](docs/dev-specs/overview.md) | Spec index and PRD-to-spec mapping |
| [docs/dev-specs/architecture.md](docs/dev-specs/architecture.md) | System architecture and channel flow |
| [docs/dev-specs/flash-memory-layout.md](docs/dev-specs/flash-memory-layout.md) | Flash/partition layouts, PM-to-DTS migration rationale, OTA compatibility limits |
| [docs/dev-specs/button-module.md](docs/dev-specs/button-module.md) | Button module behavior |
| [docs/dev-specs/network-module.md](docs/dev-specs/network-module.md) | Wi-Fi/network event lifecycle |
| [docs/dev-specs/app-wifi-prov-ble-module.md](docs/dev-specs/app-wifi-prov-ble-module.md) | BLE provisioning wrapper |
| [docs/dev-specs/heap-monitor-module.md](docs/dev-specs/heap-monitor-module.md) | Heap monitoring behavior |
| [docs/dev-specs/app-memfault-module.md](docs/dev-specs/app-memfault-module.md) | Memfault integration wrapper |
| [docs/dev-specs/app-https-client-module.md](docs/dev-specs/app-https-client-module.md) | HTTPS module behavior |
| [docs/dev-specs/app-mqtt-client-module.md](docs/dev-specs/app-mqtt-client-module.md) | MQTT module behavior |
| [docs/dev-specs/ntp-module.md](docs/dev-specs/ntp-module.md) | NTP time synchronization — SNTP client, system clock set, UTC log timestamps |
| [docs/qa-test/README.md](docs/qa-test/README.md) | QA snapshot folder conventions |

## Methodology

This project was developed using the [chsh-ncs-workflow skill](https://github.com/chshzh/claude/blob/main/skills/chsh-sk-ncs-workflow/SKILL.md) — a four-phase lifecycle for NCS/Zephyr IoT projects where each phase has a dedicated AI skill:

| Phase | Focus | Skill | Output |
|-------|-------|-------|--------|
| 1 — Product Definition | What the device should do, for whom, and why | `chsh-pm-prd` | `docs/pm-prd/PRD.md` |
| 2 — Technical Design | Translate PRD into engineering specs | `chsh-dev-spec` | `docs/dev-specs/*.md` |
| 3 — Implementation | Implement code from approved specs | `chsh-dev-project` | `src/`, passing build |
| 4 — QA & Test | Validate the build against PRD criteria | `chsh-qa-test` | `docs/qa-test/QA-*.md` |

Each phase feeds the next: requirements drive specs, specs drive code, code drives tests. Issues loop back to the right phase — code bugs to Phase 3, spec gaps to Phase 2, new requirements to Phase 1.

---

## License

[SPDX-License-Identifier: LicenseRef-Nordic-5-Clause](LICENSE)
