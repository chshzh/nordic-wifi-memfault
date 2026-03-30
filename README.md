# nordic-wifi-memfault sample

A comprehensive Memfault integration reference for Nordic Wi-Fi platforms, demonstrating IoT device management with Wi-Fi connectivity, Wi-Fi provisioning over BLE, HTTPS/MQTT communication, and cloud-based monitoring.

## Platform Support

| Board | Host MCU | Wi-Fi | BLE | Status |
|-------|----------|-------|-----|--------|
| [nRF7002DK](https://www.nordicsemi.com/Products/Development-hardware/nRF7002-DK) | nRF5340 (dual-core) | nRF7002 | Network core (`hci_ipc`) | ✅ Supported |
| [nRF54LM20DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54LM20-DK) + nRF7002EB II | nRF54LM20 (single-core) | nRF7002 via EB II shield | SoftDevice Controller (same core) | ✅ Supported |
| [nRF7120DK](https://www.nordicsemi.com/Products/Development-hardware/nRF7120-DK) | nRF7120 (integrated) | nRF7120 | — | 🔜 Planned |

> All platform-specific build targets, overlays, and partition maps live under board-named files. The application core (modules, Memfault integration) is shared across all boards.

## Overview

- **SDK**: nRF Connect SDK v3.2.4 (workspace application)
- **Memfault SDK**: bundled with NCS v3.2.4

### Key Features

- ✅ **Wi-Fi Connectivity** — WPA2/WPA3 with automatic reconnection
- ✅ **WiFi Provisioning over BLE** — Credential configuration via nRF Wi-Fi Provisioner app
- ✅ **Crash Reporting** — Automatic coredump collection and upload
- ✅ **Metrics Collection** — WiFi stats, stack usage, heap, CPU temperature
- ✅ **Heap Monitor** — Auto-detects system and mbedTLS heaps, logs in a shared format
- ✅ **nRF70 WiFi Diagnostics** — Firmware statistics (PHY/LMAC/UMAC) via CDR
- ✅ **OTA Updates** — Secure firmware updates via Memfault cloud
- ✅ **MCUboot Bootloader** — Dual-bank updates with rollback protection
- ✅ **HTTPS Test** — Periodic `HEAD` requests to `example.com`
- ✅ **MQTT Test** — TLS-secured echo test to `broker.emqx.io`

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
west flash --erase
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

| Button | Press | Action |
|--------|-------|--------|
| **Button 1** | Short (< 3 s) | Trigger Memfault heartbeat + nRF70 stats CDR upload |
| **Button 1** | Long (≥ 3 s) | Stack overflow crash (test crash reporting) |
| **Button 2** | Short (< 3 s) | Check for OTA update |
| **Button 2** | Long (≥ 3 s) | Division by zero crash (test fault handler) |

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

## Board-Specific Configuration

### nRF54LM20DK — `boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`

Overrides from `prj.conf` that are specific to this hardware:

| Setting | Value | Reason |
|---------|-------|--------|
| `NRF70_SR_COEX` | `n` | RF switch not wired on nRF7002EB II |
| `NRF70_SR_COEX_RF_SWITCH` | `n` | Same |
| `SPI_NOR` | `y` | External flash driver for MX25R6435F |
| `PM_EXTERNAL_FLASH_MCUBOOT_SECONDARY` | `y` | Use ext flash for MCUboot secondary slot |
| `FLASH_SHELL` | `y` | Debug external flash via shell |
| `SHELL` | `y` | Debug/dev override (off in prj.conf for nRF5340 flash budget) |
| `MAIN_STACK_SIZE` | `5200` | Larger: single-core BLE + WiFi |
| `NET_RX/TX_STACK_SIZE` | `4096` | Larger: same reason |

### nRF54LM20DK — `sysbuild/mcuboot/boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`

| Setting | Value | Reason |
|---------|-------|--------|
| `SPI_NOR_SFDP_DEVICETREE` | `y` | MX25R64 uses 4 KB erase sectors (not 64 KB) |
| `SPI_NOR_FLASH_LAYOUT_PAGE_SIZE` | `4096` | Must match physical sector size |
| `BOOT_MAX_IMG_SECTORS` | `512` | Covers 1908 KB / 4 KB = 477 sectors |

---

## Memory Usage

### nRF7002DK

| Region | Used | Size | % |
|--------|------|------|---|
| App FLASH | ~890 KB | 941 KB | ~94% |
| App SRAM | ~422 KB | 448 KB | ~94% |
| Net FLASH (`hci_ipc`) | ~152 KB | 256 KB | ~59% |
| Net SRAM | ~39 KB | 64 KB | ~61% |

### nRF54LM20DK (all modules enabled)

| Region | Used | Size | % |
|--------|------|------|---|
| App FLASH | ~1020–1100 KB | 1918 KB | ~53–57% |
| App SRAM | ~390–410 KB | 511 KB | ~76–80% |

> nRF54LM20DK has ample headroom in both flash (1918 KB slot vs ~1100 KB used) and SRAM (511 KB vs ~400 KB used).

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

## SRAM Layout — nRF5340 (nRF7002DK)

```
┌─────────────────────────────────────────────┐
│  0x20000000  sram_primary   448 KB          │ App: stack, heap, BSS
│  0x20070000  rpmsg_nrf53     64 KB          │ IPC shared memory (App ↔ Net core)
│  0x20080000 ── end ─────────────────────────│
└─────────────────────────────────────────────┘
```
