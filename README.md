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
west build -b nrf7002dk/nrf5340/cpuapp -p -- \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"
west flash --erase
```

**nRF54LM20DK + nRF7002EB II:**
```bash
west build -b nrf54lm20dk/nrf54lm20a/cpuapp -p -- \
  -DSHIELD=nrf7002eb2 \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-info.conf"
west flash --recover
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

### nRF7002DK (nRF5340 — 1 MB internal flash)

```
┌─────────────────────────────────────────────┐
│  0x00000  mcuboot          40 KB  (0x0A000) │ Bootloader
│  0x0A000  mcuboot_pad     512 B   (0x00200) │ Image header
│  0x0A200  app             ~912 KB (0xE3E00) │ Main application
│  0xEE000  settings_storage  8 KB  (0x02000) │ WiFi credentials / NVS
│  0xF0000  memfault_storage 64 KB  (0x10000) │ Crash coredumps
│  0x100000 ── end of internal flash ─────────│
└─────────────────────────────────────────────┘

External flash — MX25R64 (8 MB):
┌─────────────────────────────────────────────┐
│  0x000000  mcuboot_secondary  912 KB        │ OTA update slot
│  0x0E4000  external_flash    ~7.1 MB        │ Reserved
└─────────────────────────────────────────────┘
```

### nRF54LM20DK (nRF54LM20A — 1984 KB RRAM)

```
┌─────────────────────────────────────────────────┐
│  0x000000  mcuboot            56 KB (0x0E000)   │ Bootloader
│  0x00E000  mcuboot_pad         2 KB (0x00800)   │ Image header
│  0x00E800  app              1906 KB (0x1DC800)  │ Main application
│  0x1EB000  settings_storage    8 KB (0x02000)   │ WiFi credentials / NVS
│  0x1ED000  memfault_storage   64 KB (0x10000)   │ Crash coredumps
│  0x1FD000 ── end of flash_primary ──────────────│
└─────────────────────────────────────────────────┘

External flash — MX25R6435F (8 MB via spi00):
┌─────────────────────────────────────────────────┐
│  0x000000  mcuboot_secondary 1908 KB (0x1DD000) │ OTA slot (= primary size)
│  0x1DD000  external_flash   ~6.1 MB (0x623000)  │ Reserved
└─────────────────────────────────────────────────┘
```

> `memfault_storage` must be in internal flash (RRAM/NOR) for power-fail safety and boot-time access before the external flash driver initialises.

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
   - **nRF7002DK**: `build/memfault-nrf7002dk/zephyr/zephyr.elf`
   - **nRF54LM20DK**: `build_nrf54lm20dk_wifi/nordic-wifi-memfault/zephyr/zephyr.elf`
   - Dashboard → **Fleet** → **Symbol Files** → Upload

2. Set firmware version in `prj.conf`:
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

1. Update version in `prj.conf`:
   ```properties
   CONFIG_MEMFAULT_NCS_FW_VERSION="2.0.0"
   ```
2. Build and upload `zephyr.elf` (symbols) + `dfu_application.zip` (OTA payload) to Memfault dashboard.
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
