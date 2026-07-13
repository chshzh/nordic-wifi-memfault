# nordic-wifi-memfault sample

[![Build and Test](https://github.com/chshzh/nordic-wifi-memfault/actions/workflows/build.yml/badge.svg)](https://github.com/chshzh/nordic-wifi-memfault/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/chshzh/nordic-wifi-memfault?label=Release&color=brightgreen)](https://github.com/chshzh/nordic-wifi-memfault/releases/latest)
[![License](https://img.shields.io/badge/License-LicenseRef--Nordic--5--Clause-blue.svg)](LICENSE)
[![NCS Version](https://img.shields.io/badge/NCS-v2.6.4-green.svg)](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK)
![Nordic Semiconductor](https://img.shields.io/badge/Nordic%20Semiconductor-nRF7002DK-blue)
![Nordic Semiconductor](https://img.shields.io/badge/Nordic%20Semiconductor-nRF54LM20DK-red)

## Project Overview

### Introduction

`nordic-wifi-memfault` is a Memfault device-observability reference for Nordic Wi-Fi platforms. It connects to Wi-Fi, lets you provision credentials from your phone over Bluetooth LE, and continuously reports crash reports, metrics, and heap health to the Memfault cloud dashboard — with secure OTA firmware updates delivered the same way.

### Supported hardware

| Board | Build target | Status |
|-------|--------------|--------|
| nRF7002DK | `nrf7002dk_nrf5340_cpuapp` | ✅ Actively maintained — primary focus |
| nRF54LM20DK + nRF7002EB II | `nrf54lm20dk_nrf54lm20a_cpuapp` + `-DSHIELD=nrf7002eb2` | ⚠️ Not actively maintained — kept for reference, may drift out of date |

> **Project focus is now nRF7002DK.** nRF54LM20DK + nRF7002EB II support is no longer actively maintained — the build target, overlays, and partition map are left in place for reference, but new features and fixes are only verified on nRF7002DK going forward.

> This `ncs264` branch targets **NCS v2.6.4** and uses the legacy underscore board-target format shown above (not the newer `nrf7002dk/nrf5340/cpuapp` hardware-model format used on NCS v2.7+).

### Features

- **Wi-Fi STA connectivity** — connects to your WPA2/WPA3 network with automatic reconnection and NVS credential persistence.
- **Wi-Fi provisioning over BLE** — set Wi-Fi credentials from the nRF Wi-Fi Provisioner mobile app, no shell or cable needed.
- **Crash reporting** — automatic coredump capture and upload to the Memfault dashboard, with two built-in demo crashes (stack overflow, division by zero) to try it out.
- **OTA updates** — secure MCUboot-based firmware updates delivered via the Memfault cloud, checked on demand, on connect, and periodically.
- **Metrics & heartbeats** — Wi-Fi signal/channel/AP-vendor, per-thread stack headroom, heap usage, and HTTPS/MQTT success counters, all visible on the Memfault dashboard.
- **Heap monitor** — tracks system heap and mbedTLS heap usage live and feeds it into Memfault metrics, with a configurable warning threshold.
- **nRF70 Wi-Fi diagnostics (CDR)** — PHY/LMAC/UMAC firmware statistics uploaded as a Memfault Custom Data Recording for remote link-quality debugging.
- **Always-on HTTPS/MQTT clients** — periodic HTTPS `HEAD` requests and a TLS MQTT echo test, both used as background connectivity health checks with success/failure metrics.
- **nRF7002DK-focused, dual-board capable** — nRF7002DK is the actively maintained target; the same application also builds for the nRF54LM20DK + nRF7002EB II shield, though that target is no longer actively maintained (see [Supported hardware](#supported-hardware)).

### Target Users

- **Evaluator** — grab a pre-built `.hex` from the [Releases](https://github.com/chshzh/nordic-wifi-memfault/releases/latest) page, flash it, and follow the [Evaluator Quick Start](#evaluator-quick-start) guide to see live data on the Memfault dashboard in under 5 minutes.
- **Developer** — clone the workspace, build from source, and customise the firmware; see [Developer Guide](#developer-guide) for build setup and [Documentation](#documentation) for product requirements, architecture, and per-module specs.

---

## Evaluator Quick Start

### Step 1 — Flash the firmware

Download a pre-built release from the [Releases page](https://github.com/chshzh/nordic-wifi-memfault/releases/latest). Each release publishes **6 files per Memfault project** (`nord_project` / `terr_project`) — 3 per hardware target, already built with that project's Memfault key baked in:

| Suffix | Description |
|--------|--------------|
| `*_nrf7002dk_zephyr.elf` | Debug symbol file for nRF7002DK |
| `*_nrf7002dk_zephyr.signed.bin` | OTA image for nRF7002DK |
| `*_nrf7002dk_full.hex` | Full flash image for nRF7002DK (CPUAPP + CPUNET merged) |
| `*_nrf54lm20dk_zephyr.elf` | Debug symbol file for nRF54LM20DK |
| `*_nrf54lm20dk_zephyr.signed.bin` | OTA image for nRF54LM20DK |
| `*_nrf54lm20dk_full.hex` | Full flash image for nRF54LM20DK (MCUboot + app merged) |

**nRF7002DK** — erase and program all cores in one step:
```bash
nrfutil device program --firmware <project>_<ver>_nrf7002dk_full.hex --core All --erase-all
```
Or with nRF Connect for Desktop Programmer: select the `_nrf7002dk_full.hex` file, enable **Erase all**, click **Program**.

**nRF54LM20DK + nRF7002EB II** (⚠️ not actively maintained — best effort only) — RRAMC requires recovery to unlock protected MCUboot regions:
```bash
nrfutil device program --firmware <project>_<ver>_nrf54lm20dk_full.hex --recover
```

**Provision Wi-Fi** — use the **nRF Wi-Fi Provisioner** app ([Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.wifi.provisioning) | [iOS](https://apps.apple.com/app/nrf-wi-fi-provisioner/id1638948698)): connect to the device named `PV<MAC>` (e.g. `PV00D318`), select your network, and enter the password. Credentials are saved to NVS and persist across reboots.

### Step 2 — Verify

**1. UART log** — open a serial terminal at 115200 baud:

| Board | Port | Baud |
|-------|------|------|
| nRF7002DK | VCOM1 (`/dev/tty.usbmodem*3`) | 115200 |
| nRF54LM20DK + nRF7002EB II | VCOM0 (`/dev/tty.usbmodem*1`) | 115200 |

You should see the boot banner (board name, firmware version, MAC address), the enabled-module list, and — once Wi-Fi is provisioned — `[WiFi] WiFi is connected!` followed by `Sending already captured data to Memfault`.

**2. Buttons & LEDs**

### Buttons

The application uses the first two logical buttons (`DK_BTN1` / `DK_BTN2`); physical labels differ by board:

| nRF7002DK label | nRF54LM20DK label | Press | Action |
|-----------------|-------------------|-------|--------|
| Button 1 | BUTTON 0 | Short (< 3 s) | Trigger Memfault heartbeat + nRF70 stats CDR upload |
| Button 1 | BUTTON 0 | Long (≥ 3 s) | Stack overflow crash demo (test crash reporting) |
| Button 2 | BUTTON 1 | Short (< 3 s) | Check for OTA update |
| Button 2 | BUTTON 1 | Long (≥ 3 s) | Division-by-zero crash demo (test fault handler) |
| Button 3 | — | Short | Increment a demo Memfault metric |
| Button 4 | — | Short | Emit a demo Memfault trace event |

> On the nRF54LM20DK PCB the buttons are silk-printed **BUTTON 0 – BUTTON 3**. `DK_BTN1` maps to **BUTTON 0** and `DK_BTN2` maps to **BUTTON 1**. BUTTON 2/3 are not wired to any action in this release.

### LEDs

This release does not control any board LEDs — there is no LED module yet (see [PRD §8 Out of Scope](docs/pm-prd/PRD.md)).

**3. Application logic** — on the Memfault dashboard (**Fleet**):
- Confirm the device appears and a heartbeat metric (e.g. `wifi_rssi`, `ncs_system_heap_used`) has reported.
- Long-press Button 1 or Button 2 to trigger a demo crash, then confirm a coredump/trace appears after the device reboots and reconnects (upload symbol files first — see [Developer Notes](#developer-notes)).
- Short-press Button 2 (or wait for the periodic check) and confirm an OTA check log line appears; if a release is active for the project, the device downloads and installs it.

---

## Developer Guide

### Project Structure

```
nordic-wifi-memfault/
├── west.yml                                    ← Workspace manifest
├── CMakeLists.txt
├── Kconfig                                     ← Sources per-module Kconfig files
├── prj.conf                                    ← Shared config for all boards
├── sysbuild.conf
├── boards/
│   ├── nrf7002dk_nrf5340_cpuapp.conf           ← nRF7002DK Kconfig overrides
│   ├── nrf7002dk_nrf5340_cpuapp.overlay        ← nRF7002DK DTS overlay
│   ├── nrf54lm20dk_nrf54lm20a_cpuapp.conf      ← nRF54LM20DK Kconfig overrides
│   └── nrf54lm20dk_nrf54lm20a_cpuapp.overlay   ← nRF54LM20DK DTS overlay
├── pm_static_nrf7002dk_nrf5340_cpuapp.yml      ← nRF7002DK partition map (legacy Partition Manager)
├── pm_static_nrf54lm20dk_nrf54lm20a_cpuapp.yml ← nRF54LM20DK partition map
├── sysbuild/                                   ← MCUboot, hci_ipc per-board config
├── docs/
│   ├── pm-prd/PRD.md                           ← Product requirements
│   ├── dev-specs/                              ← Engineering specs (see Documentation below)
│   └── qa-test/                                ← Verification/validation reports (once created)
├── src/
│   ├── main.c                                  ← Boot banner + enabled-module log (no feature logic)
│   └── modules/
│       ├── messages.h                          ← Zbus channel message types
│       ├── button/                             ← Button SMF state machine, BUTTON_CHAN
│       ├── network/                            ← Wi-Fi L2/L3 event mgmt, WIFI_CHAN / NETWORK_CHAN
│       ├── heap_monitor/                       ← System + mbedTLS heap monitor
│       ├── wifi_prov_over_ble/                 ← BLE Wi-Fi provisioning
│       ├── app_memfault/                       ← Memfault: core upload, metrics, OTA, nRF70 CDR
│       ├── app_https_client/                   ← Periodic HTTPS HEAD requests
│       └── app_mqtt_client/                    ← TLS MQTT echo test
├── overlay-app-memfault-project-info.conf      ← Memfault key (git-ignored, from template)
└── overlay-app-memfault-project-info.conf.template
```

### Workspace Setup

West workspace is driven by [west.yml](west.yml). This `ncs264` branch is developed and built directly inside an existing **NCS v2.6.4** installation (`/opt/nordic/ncs/v2.6.4/`) rather than via a fresh `west init`; `west.yml` itself still pins `sdk-nrf` to `v3.2.4` (inherited from the `main` branch's newer-NCS target) and is not consulted when building this way. Release tags (e.g. `3.2.0`, `3.1.3`) follow project semantic versioning, independent of the NCS version — check the branch/README, not the tag, to know which NCS version a build targets.

Use nRF Connect for VS Code or a shell initialized with the NCS toolchain.

#### Method 1 (Preferred) — Add to an existing NCS installation

If you already have NCS v2.6.4 installed, reuse it directly:

```sh
cd /opt/nordic/ncs/v2.6.4   # your existing NCS v2.6.4 workspace root

git clone https://github.com/chshzh/nordic-wifi-memfault.git
cd nordic-wifi-memfault
git checkout ncs264

# This app is built in place; no west.yml manifest switch or `west update` is required
# because it lives inside an NCS tree that already has `nrf`, `zephyr`, etc. checked out.
```

#### Method 2 — Fresh installation as a Workspace Application

##### Option A: nRF Connect for VS Code

Follow the [custom repository guide](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/extension_custom_repo.html), selecting NCS v2.6.4.

##### Option B: CLI

```sh
west init -m https://github.com/chshzh/nordic-wifi-memfault.git --mr ncs264 <workspace-dir>
cd <workspace-dir>
west update
```

> Building this way pulls `sdk-nrf` per `west.yml` (currently `v3.2.4`), which does **not** match this branch's actual v2.6.4 target — Method 1 (an existing v2.6.4 install) is strongly preferred until `west.yml` is corrected for this branch.

### Build

First, set the Memfault project key:

```bash
cp overlay-app-memfault-project-info.conf.template overlay-app-memfault-project-info.conf
# Edit the file and set CONFIG_MEMFAULT_NCS_PROJECT_KEY
```

```bash
# nRF7002DK (primary, actively maintained)
west build -b nrf7002dk_nrf5340_cpuapp -d build_nrf7002dk -p --sysbuild -- \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"

# nRF54LM20DK + nRF7002EB II (not actively maintained — best effort only)
west build -b nrf54lm20dk_nrf54lm20a_cpuapp -d build_nrf54lm20dk -p --sysbuild -- \
  -DSHIELD=nrf7002eb2 \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"
```

> `--sysbuild` must be passed explicitly — NCS v2.6.4 does not default to sysbuild the way v2.7+ does. Without it, MCUboot silently never gets built even though `sysbuild.conf` requests it, and the board boots into a hardfault with zero UART output.
>
> Board targets use the legacy underscore format (`nrf7002dk_nrf5340_cpuapp`), not the newer `nrf7002dk/nrf5340/cpuapp` hardware-model format.
>
> If the build fails with `Failed to import nanopb_pb2.py` / `AttributeError: module 'google.protobuf.reflection' has no attribute 'MakeClass'`, your Python environment's `protobuf`/`grpcio-tools` user-site-packages are shadowing the toolchain's bundled `nanopb` generator. Fix: `export PYTHONNOUSERSITE=1` before building (or add it to your shell profile).

### Flash

First-time flash (erases NVS — Wi-Fi credentials must be re-provisioned):

```bash
# nRF7002DK
west flash -d build_nrf7002dk --erase

# nRF54LM20DK + nRF7002EB II
west flash -d build_nrf54lm20dk --recover
```

Subsequent flash (preserves credentials and settings):

```bash
# nRF7002DK
west flash -d build_nrf7002dk

# nRF54LM20DK + nRF7002EB II
west flash -d build_nrf54lm20dk
```

### Developer Notes

- **Project focus is nRF7002DK** — nRF54LM20DK + nRF7002EB II support is no longer actively maintained. It builds and the code paths remain in the repo, but changes are not routinely re-verified on that board; treat it as best-effort/reference only.
- **Single-core BLE + Wi-Fi (nRF54LM20DK, not actively maintained)** — the nRF54LM20A has no network core. Both the BLE SoftDevice Controller and the nRF70 Wi-Fi driver run on the application core; during BLE provisioning both stacks share the BT RX workqueue thread (`CONFIG_BT_RX_STACK_SIZE=22000`, set by `wifi_prov_over_ble` module defaults).
- **Board differences** — see [docs/dev-specs/1-architecture.md](docs/dev-specs/1-architecture.md) and [docs/dev-specs/2-dts-partition.md](docs/dev-specs/2-dts-partition.md) for the full nRF7002DK vs. nRF54LM20DK comparison (SoC, BLE host, flash/RRAM layout, UART routing).
- **Default runtime state** — with no Wi-Fi credentials stored, the device advertises as `PV<MAC>` for BLE provisioning on every boot; once credentials exist, it reconnects automatically and BLE provisioning remains available for re-provisioning.
- **Flash/RRAM partition layout** — see [docs/dev-specs/2-dts-partition.md](docs/dev-specs/2-dts-partition.md); this project stays on the legacy Zephyr Partition Manager (`pm_static_<board>.yml`), not DTS fixed-partitions (NCS v3.3+).
- **Memfault symbol files** — upload the matching `zephyr.elf` (from your own build, or the release's `*_zephyr.elf`) under **Fleet → Symbol Files** so the dashboard can decode stack traces and coredumps. Without it, coredumps and OTA-related crash traces won't symbolicate.
- **Log interpretation** — the boot banner prints board name, firmware version (`CONFIG_MEMFAULT_NCS_FW_VERSION`), build date/time, MAC address, and the list of enabled modules — useful for confirming which optional features (HTTPS/MQTT clients, nRF70 CDR) are compiled in.
- **Metrics reference** — key Memfault metrics: `wifi_rssi`, `wifi_sta_*` (channel/beacon/DTIM/TWT), `wifi_ap_oui_vendor`, `ncs_system_heap_*`, `ncs_mbedtls_heap_*`, `stack_free_*`, `app_https_req_total_count`/`app_https_req_fail_count`, `app_mqtt_echo_total_count`/`app_mqtt_echo_fail_count` — see [docs/dev-specs/app-memfault-module.md](docs/dev-specs/app-memfault-module.md), [docs/dev-specs/heap-monitor-module.md](docs/dev-specs/heap-monitor-module.md).
- **OTA versioning** — bump `CONFIG_MEMFAULT_NCS_FW_VERSION` in `prj.conf` before building a release; the device checks for updates on Wi-Fi connect, on Button 2 short-press, and periodically (`CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN`).

---

## Documentation

The full design documentation lives under `docs/`. Start with [docs/dev-specs/0-overview.md](docs/dev-specs/0-overview.md), which maps every PRD requirement to the spec file that implements it and provides an architecture summary.

| Document | Description |
|---|---|
| [docs/pm-prd/PRD.md](docs/pm-prd/PRD.md) | Product Requirements — user perspective features, behavior, acceptance criteria, changelog |
| [docs/dev-specs/0-overview.md](docs/dev-specs/0-overview.md) | **Start here** — technical spec index, PRD-to-spec mapping, architecture summary, design decisions |
| [docs/dev-specs/1-architecture.md](docs/dev-specs/1-architecture.md) | System architecture — module map, Zbus channels, boot sequence, memory budget |
| [docs/dev-specs/2-dts-partition.md](docs/dev-specs/2-dts-partition.md) | Flash/RRAM partition layout per board (legacy Partition Manager) |
| [docs/dev-specs/3-memopt.md](docs/dev-specs/3-memopt.md) | Memory optimization — stack watermarks, heap budget, headroom |
| [docs/dev-specs/button-module.md](docs/dev-specs/button-module.md) | Button SMF state machine, press actions |
| [docs/dev-specs/network-module.md](docs/dev-specs/network-module.md) | Wi-Fi STA connectivity, L2/L3 event management |
| [docs/dev-specs/app-wifi-prov-ble-module.md](docs/dev-specs/app-wifi-prov-ble-module.md) | Wi-Fi credential provisioning via BLE |
| [docs/dev-specs/app-memfault-module.md](docs/dev-specs/app-memfault-module.md) | Memfault core, metrics, OTA triggers, nRF70 stats CDR |
| [docs/dev-specs/app-https-client-module.md](docs/dev-specs/app-https-client-module.md) | Always-on periodic HTTPS client |
| [docs/dev-specs/app-mqtt-client-module.md](docs/dev-specs/app-mqtt-client-module.md) | Always-on TLS MQTT echo client |
| [docs/dev-specs/heap-monitor-module.md](docs/dev-specs/heap-monitor-module.md) | System/mbedTLS heap tracking, Memfault heap metrics |

---

## Methodology

Developed with [chsh-sk-ncs-0-workflow](https://github.com/chshzh/claude/blob/main/skills/chsh-sk-ncs-0-workflow/SKILL.md) — a four-phase PRD → Specs → Implementation → V&V lifecycle for NCS/Zephyr IoT projects.

---

## License

[SPDX-License-Identifier: LicenseRef-Nordic-5-Clause](LICENSE)
