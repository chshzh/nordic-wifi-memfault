# nordic-wifi-memfault sample

[![Build and Test](https://github.com/chshzh/nordic-wifi-memfault/actions/workflows/build.yml/badge.svg)](https://github.com/chshzh/nordic-wifi-memfault/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/chshzh/nordic-wifi-memfault?label=Release&color=brightgreen)](https://github.com/chshzh/nordic-wifi-memfault/releases/latest)
[![License](https://img.shields.io/badge/License-LicenseRef--Nordic--5--Clause-blue.svg)](LICENSE)
[![NCS Version](https://img.shields.io/badge/NCS-v3.2.4-green.svg)](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK)
![Nordic Semiconductor](https://img.shields.io/badge/Nordic%20Semiconductor-nRF7002DK-blue)
![Nordic Semiconductor](https://img.shields.io/badge/Nordic%20Semiconductor-nRF54LM20DK-red)

A comprehensive **Memfault** integration reference for Nordic Wi-Fi platforms, demonstrating IoT device observability with Wi-Fi connectivity, Wi-Fi provisioning over BLE, HTTPS/MQTT communication, and Memfault cloud-based crash reporting, metrics, and OTA updates.

## Platform Support

| Board | Host MCU | Wi-Fi | BLE | Status |
|-------|----------|-------|-----|--------|
| [nRF7002DK](https://www.nordicsemi.com/Products/Development-hardware/nRF7002-DK) | nRF5340 (dual-core) | nRF7002 | Network core (`hci_ipc`) | ✅ Supported |
| [nRF54LM20DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54LM20-DK) + [nRF7002 EBII](https://www.nordicsemi.com/Products/Development-hardware/nRF7002-EBII) | nRF54LM20 (single-core) | nRF7002 via EB II shield | SoftDevice Controller (same core) | ✅ Supported |
| [nRF7120DK](https://www.nordicsemi.com/Products/Development-hardware/nRF7120-DK) | nRF7120 (integrated) | nRF7120 | — | 🔜 Planned |

> All platform-specific build targets, overlays, and partition maps live under board-named files. The application core (modules, Memfault integration) is shared across all boards.

## Overview

- **SDK**: nRF Connect SDK v3.2.4 (workspace application)
- **Memfault SDK**: bundled with NCS v3.2.4

### Key Features

#### Memfault Device Observability
- ✅ **Crash Reporting** — Automatic coredump capture (internal flash / RRAM) and upload to Memfault dashboard
- ✅ **OTA Updates** — Secure MCUboot-based firmware updates deployed via Memfault cloud
- ✅ **Metrics & Heartbeats** — WiFi signal, channel, AP vendor, stack headroom, heap usage, HTTP/MQTT counters
- ✅ **Heap Monitor** — Auto-tracks system heap and dedicated mbedTLS heap; feeds live values into Memfault metrics
- ✅ **nRF70 Diagnostics CDR** — PHY/LMAC/UMAC firmware statistics uploaded as Custom Data Recordings

#### Connectivity & Provisioning
- ✅ **Wi-Fi Connectivity** — WPA2/WPA3 with automatic reconnection and NVS credential persistence
- ✅ **WiFi Provisioning over BLE** — Credential configuration via the nRF Wi-Fi Provisioner mobile app
- ✅ **HTTPS Client** — Periodic TLS `HEAD` requests to `example.com` with success/failure metrics
- ✅ **MQTT Client** — TLS-secured echo test to `broker.emqx.io` with success/failure metrics

#### Platform
- ✅ **Dual Hardware** — nRF7002DK (nRF5340 dual-core) and nRF54LM20DK + nRF7002EB II (nRF54LM20A single-core)
- ✅ **MCUboot Bootloader** — Dual-bank updates with rollback protection, external-flash OTA slot

---

## Quick Start

### 1. Set Memfault project key

```bash
cp overlay-app-memfault-project-info.conf.template overlay-app-memfault-project-info.conf
# Edit the file and set CONFIG_MEMFAULT_NCS_PROJECT_KEY
```

### 2. Build and flash

**nRF7002DK:**
```bash
west build -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -p -- \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"
west flash -d build_nrf7002dk --erase
```

**nRF54LM20DK + nRF7002EB II:**
```bash
west build -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -p -- \
  -DSHIELD=nrf7002eb2 \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"
west flash -d build_nrf54lm20dk --recover
```

### 3. Provision WiFi

Use the **nRF Wi-Fi Provisioner** app: [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.wifi.provisioning) | [iOS](https://apps.apple.com/app/nrf-wi-fi-provisioner/id1638948698)

- Connect to device named `PV<MAC>` (e.g., `PV00D318`)
- Select your network and enter the password
- The device connects and saves credentials to NVS — they persist across reboots

### 4. Monitor via UART

| Board | Port | Baud |
|-------|------|------|
| nRF7002DK | VCOM1 (`/dev/tty.usbmodem*3`) | 115200 |
| nRF54LM20DK | VCOM0 (`/dev/tty.usbmodem*1`) | 115200 |

---

## Workspace Setup

### Method 1 (Preferred) — Add to an existing NCS installation

If you already have a matching NCS version installed, reuse it directly — no re-downloading required.

Under a terminal with the toolchain:

```sh
cd /opt/nordic/ncs/<ncs-version>   # your existing NCS workspace root

git clone https://github.com/chshzh/nordic-wifi-memfault.git

# Switch the workspace manifest to nordic-wifi-memfault (one-time change)
west config manifest.path nordic-wifi-memfault

# Sync — NCS repos already present, only new project repos are cloned
west update
```

### Method 2 — Fresh installation as a Workspace Application

#### Option A: nRF Connect for VS Code

Follow the [custom repository guide](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/extension_custom_repo.html).

#### Option B: CLI

See the Nordic guide on [Workspace Application Setup](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/dev_model_and_contributions/adding_code.html#workflow_4_workspace_application_repository_recommended).

```sh
west init -m https://github.com/chshzh/nordic-wifi-memfault --mr main <workspace-dir>
cd <workspace-dir>
west update
```

---

## Button Controls

The application uses the first two logical buttons (`DK_BTN1` / `DK_BTN2`).  
Their physical labels differ by board:

| nRF7002DK label | nRF54LM20DK label | Press | Action |
|-----------------|-------------------|-------|--------|
| **Button 1** | **BUTTON 0** | Short (< 3 s) | Trigger Memfault heartbeat + nRF70 stats CDR upload |
| **Button 1** | **BUTTON 0** | Long (≥ 3 s) | Stack overflow crash (test crash reporting) |
| **Button 2** | **BUTTON 1** | Short (< 3 s) | Check for OTA update |
| **Button 2** | **BUTTON 1** | Long (≥ 3 s) | Division by zero crash (test fault handler) |

> On the nRF54LM20DK PCB the buttons are silk-printed **BUTTON 0 – BUTTON 3**.  
> `DK_BTN1` maps to **BUTTON 0** and `DK_BTN2` maps to **BUTTON 1** (both are `sw0`/`sw1` in the board DTS).  
> BUTTON 2 and BUTTON 3 are not used by this application.

---

## Project Structure

```
nordic-wifi-memfault/
├── west.yml                                    # Workspace manifest (NCS v3.2.4)
├── CMakeLists.txt
├── Kconfig
├── prj.conf                                    # Shared config for all boards
├── boards/
│   ├── nrf7002dk_nrf5340_cpuapp.overlay        # nRF7002DK DTS overlay
│   ├── nrf54lm20dk_nrf54lm20a_cpuapp.conf      # nRF54LM20DK Kconfig overrides
│   └── nrf54lm20dk_nrf54lm20a_cpuapp.overlay   # nRF54LM20DK DTS overlay
├── pm_static_nrf7002dk_nrf5340_cpuapp.yml      # nRF7002DK partition map
├── pm_static_nrf54lm20dk_nrf54lm20a_cpuapp.yml # nRF54LM20DK partition map
├── sysbuild/                                   # MCUboot, hci_ipc per-board config
├── src/
│   ├── main.c                                  # Banner + sleep (modules init via SYS_INIT)
│   └── modules/
│       ├── messages.h                          # Zbus channel message types
│       ├── button/                             # Button SMF, BUTTON_CHAN
│       ├── network/                            # WiFi/network events, WIFI_CHAN
│       ├── heap_monitor/                       # System + mbedTLS heap monitor
│       ├── wifi_prov_over_ble/                 # BLE WiFi provisioning
│       ├── app_memfault/                       # Memfault: upload, metrics, OTA, CDR
│       ├── app_https_client/                   # Periodic HTTPS HEAD to example.com
│       └── app_mqtt_client/                    # TLS MQTT echo test
├── overlay-app-memfault-project-info.conf      # Memfault key (git-ignored, from template)
└── overlay-app-memfault-project-info.conf.template
```

---

## Flash Memory Layout

**NCS v3.3+ Migration Note:** As of NCS v3.3.0, this project has migrated from the legacy **Partition Manager** (PM) build-time system to **DeviceTree fixed-partitions** (DTS-driven). This aligns with NCS 3.3's deprecation of PM. The layouts below reflect the **new DTS-based configuration** deployed via overlay files.

### nRF7002DK (nRF5340 — 1 MB internal flash)

#### New Layout (DTS-based, NCS 3.3+)

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x00000` | `boot_partition` | 40 KB | Bootloader (MCUboot) |
| `0x0A000` | `slot0_partition` | 920 KB | Primary app image |
| `0xEE000` | `storage_partition` | 8 KB | WiFi credentials / NVS |
| `0xF0000` | `memfault_storage` | 64 KB | Crash coredumps |

**External flash** — MX25R64 (8 MB):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `slot1_partition` | 920 KB | Secondary OTA slot |

#### Legacy Layout (Partition Manager, NCS 3.2 and earlier)

| Address | Partition | Size | Purpose | Notes |
|---------|-----------|------|---------|-------|
| `0x00000` | `mcuboot` | 40 KB | Bootloader | — |
| `0x0A000` | `mcuboot_pad` | 512 B | Image header padding | **Removed in DTS** |
| `0x0A200` | `app` | 912 KB | Main application | Now starts @ `0x0A000` |
| `0xEE000` | `settings_storage` | 8 KB | WiFi credentials / NVS | — |
| `0xF0000` | `memfault_storage` | 64 KB | Crash coredumps | — |

**Key difference:** The 512-byte `mcuboot_pad` is eliminated, shifting the app start address earlier by 0x200 bytes.

---

### nRF54LM20DK + nRF7002EB II (nRF54LM20A — 1984 KB RRAM)

#### New Layout (DTS-based, NCS 3.3+)

**Internal flash** — RRAM (cpuapp_rram):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `boot_partition` | 56 KB | Bootloader (MCUboot) |
| `0x0E000` | `slot0_partition` | 1844 KB | Primary app image |
| `0x1D3000` | `storage_partition` | 8 KB | WiFi credentials / NVS |
| `0x1D5000` | `memfault_coredump_partition` | 64 KB | Crash coredumps |

**External flash** — MX25R6435F (8 MB via spi00):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `slot1_partition` | 1844 KB | Secondary OTA slot |

#### Legacy Layout (Partition Manager, NCS 3.2 and earlier)

**Internal flash** — RRAM (flash_primary):

| Address | Partition | Size | Purpose | Notes |
|---------|-----------|------|---------|-------|
| `0x000000` | `mcuboot` | 56 KB | Bootloader | — |
| `0x0E000` | `mcuboot_pad` | 2 KB | Image header padding | **Removed in DTS** |
| `0x0E800` | `app` | 1810 KB | Main application | Now starts @ `0x0E000` |
| `0x1D3000` | `settings_storage` | 8 KB | WiFi credentials / NVS | — |
| `0x1D5000` | `memfault_coredump_partition` | 64 KB | Crash coredumps | — |

**External flash** — MX25R6435F (8 MB via spi00):

| Address | Partition | Size | Purpose | Notes |
|---------|-----------|------|---------|-------|
| `0x000000` | `mcuboot_secondary` | 1812 KB | OTA slot | Now 1844 KB in DTS |

---

### Migration Rationale

| Aspect | Partition Manager (v3.2−) | DeviceTree (v3.3+) | Reason |
|--------|--------------------------|-------------------|--------|
| **Configuration** | YAML (`pm_static_*.yml`) | DTS overlays (`.overlay` files) | DTS is Zephyr standard; PM is deprecated |
| **Build system** | Sysbuild (legacy PM subsystem) | Standard MCUboot DTS parsing | Simplifies toolchain; DTS-driven boot is universal |
| **Pad bytes** | Explicit `mcuboot_pad` node (512 B or 2 KB) | Eliminated; slot addresses aligned directly | Reduces complexity; recovers bytes for app |
| **Coexistence** | PM-based and DTS partitions mutual exclusive | DTS only | Cleaner migration path; single authoritative source |
| **Firmware compatibility** | Different builds use same address layout | Different address alignment per layout | **OTA across PM↔DTS boundary is not supported** |

**Why the pad was removed:**
- MCUboot's image header is now handled purely by the bootloader—no need for explicit padding in the partition map.
- DTS fixed-partitions are simpler and more flexible than PM's derivation rules.

> **Important:** Firmware compiled under PM (v3.2) cannot be OTA-updated to DTS (v3.3) and vice versa due to the 512-byte / 2 KB address shift. Devices must be reflashed via hardware or a custom dual-partition transition scheme.

---

### OTA Compatibility Matrix

**⚠️ Critical:** Firmware compiled for **PM (v3.2)** cannot be OTA-updated to **DTS (v3.3+)**, and vice versa. The address shifts break image validation in MCUboot.

#### nRF7002DK — Address Alignment Impact

| Aspect | Old (PM, v3.2−) | New (DTS, v3.3+) | Impact |
|--------|-----------------|------------------|--------|
| Boot partition | 0x0 – 0xa000 | 0x0 – 0xa000 | ✓ Same |
| **App slot start** | **0xa200** | **0xa000** | ⚠️ **512-byte shift** |
| App slot size | 0xe3e00 (912 KB) | 0xe4000 (920 KB) | Size increased |
| Settings storage | 0xee000 – 0xf0000 | 0xee000 – 0xf0000 | ✓ Same |
| Memfault storage | 0xf0000 – 0x100000 | 0xf0000 – 0x100000 | ✓ Same |
| External slot1 | 0x0 – 0xe4000 | 0x0 – 0xe4000 | ✓ Same |

**Consequence:** MCUboot validates firmware at its configured address. If a PM-built binary (@ 0xa200) is placed in a DTS-configured partition (@ 0xa000), CRC validation fails → boot loop.

#### nRF54LM20DK + nRF7002EB II — Address Alignment Impact

| Aspect | Old (PM, v3.2−) | New (DTS, v3.3+) | Impact |
|--------|-----------------|------------------|--------|
| Boot partition | 0x0 – 0xe000 | 0x0 – 0xe000 | ✓ Same |
| **App slot start** | **0xe800** | **0xe000** | ⚠️ **2 KB shift** |
| App slot size | 0x1c4800 (1810 KB) | 0x1c5000 (1844 KB) | Size increased |
| Settings storage | 0x1d3000 – 0x1d5000 | 0x1d3000 – 0x1d5000 | ✓ Same |
| Memfault coredump | 0x1d5000 – 0x1e5000 | 0x1d5000 – 0x1e5000 | ✓ Same |
| External slot1 | 0x0 – 0x1c5000 | 0x0 – 0x1c5000 | ✓ Same |

**Consequence:** Same as nRF7002DK—firmware misalignment → CRC mismatch → boot failure.

#### Migration Strategy

| Scenario | Feasible? | Method |
|----------|-----------|--------|
| **OTA from PM to DTS** | ❌ **No** | MCUboot reconfiguration breaks binary placement |
| **OTA from DTS to PM** | ❌ **No** | Binary at 0xa000/0xe000 won't validate @ 0xa200/0xe800 |
| **Hardware reflash (JLink)** | ✅ **Yes** | Full-image erase + reflash bypasses bootloader |
| **Custom dual-partition firmware** | ✅ **Yes** (complex) | Intermediate firmware relocates binaries (not recommended) |

**Recommended approach:** Use **hardware reflash via JLink** for any device migration from PM to DTS.

---

### Memfault Coredump Storage

Both layouts keep `memfault_storage` / `memfault_coredump_partition` in **internal flash** for:
- **Power-fail safety**: Coredump writes can survive power loss when backed by NOR or RRAM.
- **Boot-time access**: Memfault uploads coredumps before external flash drivers initialize.

On nRF54LM20DK, the coredump partition is backed by RRAM with an ECC controller; writes are atomic and protected.

---

## Architecture Notes

### nRF7002DK vs nRF54LM20DK

| Aspect | nRF7002DK | nRF54LM20DK + nRF7002EB II |
|--------|-----------|---------------------------|
| Host SoC | nRF5340 (dual-core) | nRF54LM20A (single-core) |
| BLE host | Network core (`hci_ipc` image) | SoftDevice Controller on App core |
| WiFi chip | nRF7002 (on-board) | nRF7002 via nRF7002EB II shield (SPI spi22) |
| SR Coex RF switch | ✅ Yes | ❌ Not wired on EB II |
| Internal flash | 1 MB NOR | 2 MB RRAM |
| External flash | MX25R64 (SPI) | MX25R6435F (spi00) |
| App slot | ~912 KB | 1906 KB |
| SRAM | 448 KB app + 64 KB IPC | 511 KB (full) |
| UART console | VCOM1 (uart0) | VCOM0 (uart30, routed via EB II) |

### Single-core BLE + WiFi (nRF54LM20DK)

The nRF54LM20A has no network core. Both the BLE SoftDevice Controller and the nRF70 WiFi driver run on the Application core. During WiFi provisioning over BLE, both stacks share the BT RX workqueue thread — hence `CONFIG_BT_RX_STACK_SIZE=22000` (set by the `wifi_prov_over_ble` module defaults).

---

## Memfault Integration

### Device Onboarding

1. Upload symbol file after building:
   - **nRF7002DK**: `build_nrf7002dk/nordic-wifi-memfault/zephyr/zephyr.elf`
   - **nRF54LM20DK**: `build_nrf54lm20dk/nordic-wifi-memfault/zephyr/zephyr.elf`
   - Dashboard → **Fleet** → **Symbol Files** → Upload

2. Set firmware version in `overlay-app-memfault-project-info.conf`:
   ```properties
   CONFIG_MEMFAULT_NCS_FW_VERSION="1.0.0"
   ```

### Collected Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `wifi_rssi` | Gauge | Signal strength (dBm) |
| `wifi_sta_*` | Gauge | Channel, beacon interval, DTIM, TWT |
| `wifi_ap_oui_vendor` | String | AP vendor (Cisco, Apple, ASUS, etc.) |
| `ncs_system_heap_*` | Gauge | System heap total, current, peak |
| `ncs_mbedtls_heap_*` | Gauge | mbedTLS heap total, current, peak |
| `stack_free_*` | Gauge | Per-thread stack headroom |
| `app_https_req_total_count` | Counter | Total HTTPS requests |
| `app_https_req_fail_count` | Counter | Failed HTTPS requests |
| `app_mqtt_echo_total_count` | Counter | Total MQTT echo attempts |
| `app_mqtt_echo_fail_count` | Counter | Failed MQTT echoes |

### OTA Updates

1. Update version in `overlay-app-memfault-project-info.conf`:
   ```properties
   CONFIG_MEMFAULT_NCS_FW_VERSION="2.0.0"
   ```
2. Build and upload `zephyr.elf` (symbols) + `zephyr.signed.bin` (OTA payload) to Memfault dashboard.
3. The device checks for updates on WiFi connect and on Button 2 short-press.

---

## Using Pre-built Release Firmware

Download the latest firmware from the [Releases page](https://github.com/chshzh/nordic-wifi-memfault/releases/latest).  
Each release publishes **6 files per Memfault project** (`nord_project` / `terr_project`) — 3 per hardware target:

### File naming

```
<project>_<version>_<board>_<type>
```

| Suffix | Description |
|--------|-------------|
| `*_nrf7002dk_zephyr.elf` | Debug symbol file for nRF7002DK |
| `*_nrf7002dk_zephyr.signed.bin` | OTA image for nRF7002DK |
| `*_nrf7002dk_full.hex` | Full flash image for nRF7002DK (CPUAPP + CPUNET merged) |
| `*_nrf54lm20dk_zephyr.elf` | Debug symbol file for nRF54LM20DK |
| `*_nrf54lm20dk_zephyr.signed.bin` | OTA image for nRF54LM20DK |
| `*_nrf54lm20dk_full.hex` | Full flash image for nRF54LM20DK (MCUboot + app merged) |

### Initial programming (first time)

**nRF7002DK** — erase and program all cores in one step:
```bash
nrfutil device program --firmware <project>_<ver>_nrf7002dk_full.hex --core All --erase-all
```
Or using nRF Programmer: select the `_nrf7002dk_full.hex` file, enable **Erase all**, click **Program**.

**nRF54LM20DK + nRF7002EB II** — RRAMC requires recovery to unlock protected MCUboot regions:
```bash
nrfutil device program --firmware <project>_<ver>_nrf54lm20dk_full.hex --recover
```

### OTA updates (subsequent updates via Memfault)

1. Upload `*_zephyr.elf` to **Memfault → Software → Symbol Files** so the dashboard can decode stack traces and coredumps.
2. Upload `*_zephyr.signed.bin` to **Memfault → Software → OTA Releases** and activate the release.
3. The device polls for and downloads the update automatically on Wi-Fi connect or when **Button 2** / **BUTTON 1** is short-pressed.

### Choosing a project file

| File prefix | Memfault project | Use for |
|-------------|-----------------|---------|
| `nord_project_*` | Nord project | Nord devices |
| `terr_project_*` | Terr project | Terr devices |

---

##  License

This project is licensed under the LicenseRef-Nordic-5-Clause license. See the `LICENSE` file for details.

## 🤝 Contributing

Contributions are welcome! Please ensure all code follows the Zephyr coding style and includes appropriate license headers.

## 📞 Support

For questions and support:
- [Nordic DevZone](https://devzone.nordicsemi.com/)
- [GitHub Issues](https://github.com/your-repo/nordic_wifi_opus_audio_demo/issues)
