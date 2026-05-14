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
- Optional HTTPS periodic check module
- Optional MQTT periodic publish/echo module
- Heap monitoring for system and mbedTLS heaps
- Modular architecture based on SYS_INIT + zbus

### Target Users

- **Evaluator** - flash release firmware and validate logs/events quickly
- **Developer** - build from source and modify modules/config

---

## Evaluator Quick Start

### Step 1 - Flash the firmware

Download pre-built firmware from the [Releases](https://github.com/chshzh/nordic-wifi-memfault/releases/latest) page and flash your board.

Pre-built firmware is only available for approved custom Memfault projects because
project keys are project-specific. If your project is not listed below, it is not
currently supported for evaluator pre-built access.

| Supported project | Release artifact prefix | Status |
|-------------------|-------------------------|--------|
| nord_project | `nord_project_*` | Supported |
| terr_project | `terr_project_*` | Supported |

For evaluation access to additional projects, contact charlie.shao@nordicsemi.no
or local Nordic Sales team. Alternatively, follow the Developer path and configure
your own project key.

### Step 2 - Provision Wi-Fi via BLE

Provision Wi-Fi credentials with [nRF Wi-Fi Provisioner app](https://www.nordicsemi.com/Products/Development-tools/nRF-Wi-Fi-Provisioner).

### Step 3 - Verify runtime behavior

Explore your device behavior in [Memfault Cloud](https://app.memfault.com/) after
the device connects and begins uploading data.

Monitor via UART:

| Board | Port | Baud |
|-------|------|------|
| nRF7002DK | VCOM1 (`/dev/tty.usbmodem*3`) | 115200 |
| nRF54LM20DK + nRF7002EB2 | VCOM0 (`/dev/tty.usbmodem*1`) | 115200 |

Use serial terminal at `115200` and verify:
- boot banner and enabled module list,
- Wi-Fi connect/network-ready logs,
- Memfault upload or heartbeat trigger logs.

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

```bash
# nRF7002DK
west flash -d build_nrf7002dk --erase

# nRF54LM20DK
west flash -d build_nrf54lm20dk --recover
```

### Serial Monitor

Connect at `115200` baud and observe module/init and connectivity logs.

### Developer Notes

- Keep `overlay-app-memfault-project-info.conf` out of git history.
- Board-specific behavior for nRF54LM20DK is configured in `boards/*.conf`.
- Coredump and partition behavior follows DTS fixed-partitions for NCS v3.3.0.

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
