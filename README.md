# Memfault nRF7002DK Sample

A comprehensive Memfault integration sample for Nordic nRF7002DK, demonstrating IoT device management with Wi-Fi connectivity, WiFi provisioning over BLE, HTTPS communication, and cloud-based monitoring.

## Overview

This sample application showcases:
- **Platform**: nRF7002DK (nRF5340 + nRF7002 WiFi companion chip)
- **SDK**: nRF Connect SDK v3.2.1 (workspace application)
- **Memfault SDK**: default in NCS v3.2.1

### Key Features

- ✅ **Wi-Fi Connectivity** - WPA2/WPA3 with automatic reconnection
- ✅ **WiFi Provisioning over BLE** - Wireless WiFi credential configuration via mobile app
- ✅ **Crash Reporting** - Automatic coredump collection and upload to cloud
- ✅ **Metrics Collection** - WiFi stats, stack usage, heap, CPU temperature
- ✅ **Reusable Heap Monitor Module** - Auto-detects system and mbedTLS heaps and logs them in a shared format
- ✅ **nRF70 WiFi Diagnostics** - Firmware statistics (PHY/LMAC/UMAC) via CDR
- ✅ **OTA Updates** - Secure firmware updates via Memfault cloud
- ✅ **MCUBoot Bootloader** - Dual-bank updates with rollback protection

### Always-Enabled Application Modules

- 📡 **App HTTPS Request Test** - Periodic HTTPS connectivity testing to `example.com`
- 📨 **App MQTT Echo Test** - TLS-secured MQTT broker connectivity testing to `test.mosquitto.org`

## Hardware Requirements

- nRF7002DK development kit
- USB cable for power and debugging
- WiFi Access Point (2.4 GHz, WPA2/WPA3)
- Memfault Account ([sign up free](https://memfault.com))

## Prerequisites

### 1. NCS workspace (recommended)

This app is a **workspace application**: NCS is fetched automatically when you init the workspace.

```bash
# From a directory that will become the workspace root (e.g. ncs-workspace)
west init -l /path/to/memfault-nrf7002dk
west update -o=--depth=1 -n
cd memfault-nrf7002dk
west build -p -b nrf7002dk/nrf5340/cpuapp
```

To use an existing NCS install (e.g. `/opt/nordic/ncs/v3.2.1`) instead:

```bash
cd /opt/nordic/ncs/v3.2.1
west build -p -b nrf7002dk/nrf5340/cpuapp /path/to/memfault-nrf7002dk
```


## Quick Start

1. **Memfault project key**: `prj.conf` uses a placeholder so the project builds without an overlay. For production, create `overlay-app-memfault-project-key.conf` from the template:
   ```bash
   cp overlay-app-memfault-project-key.conf.template overlay-app-memfault-project-key.conf
   # Edit the file and set your project key
   ```
   Add `overlay-app-memfault-project-key.conf` to `.git/info/exclude` to keep your key out of version control.

2. **Build and flash** (from NCS workspace root, or from app dir if you used `west init -l`):
   ```bash
   west build -b nrf7002dk/nrf5340/cpuapp -p -- \
     -DEXTRA_CONF_FILE="overlay-app-memfault-project-key.conf"
   west flash --erase
   ```

3. **Provision WiFi** using the nRF Wi-Fi Provisioner mobile app:
   - Download: [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.wifi.provisioning) | [iOS](https://apps.apple.com/app/nrf-wi-fi-provisioner/id1638948698)
   - Connect to device `PV<MAC>` (e.g., `PV006EB1`)
   - Select your WiFi network and enter password

4. **Monitor device** on Memfault dashboard

## Button Controls

| Button | Press | Action |
|--------|-------|--------|
| **Button 1** | Short (< 3s) | Trigger Memfault heartbeat + nRF70 stats CDR upload |
| **Button 1** | Long (≥ 3s) | Stack overflow crash (test crash reporting) |
| **Button 2** | Short (< 3s) | Check for OTA update |
| **Button 2** | Long (≥ 3s) | Division by zero crash (test fault handler) |

## Project Structure

Modular layout (SMF + zbus); all feature code lives under `src/modules/`.

```
memfault-nrf7002dk/
├── west.yml                          # Workspace manifest (NCS v3.2.1)
├── CMakeLists.txt                    # Root build (add_subdirectory modules)
├── Kconfig                           # Root Kconfig (rsource module Kconfigs)
├── prj.conf                          # Base + Memfault config
├── src/
│   ├── main.c                        # Entry: banner + sleep (init via SYS_INIT)
│   └── modules/
│       ├── messages.h                # Zbus message types (button, wifi, memfault)
│       ├── button/                   # Button SMF, BUTTON_CHAN
│       ├── network/                  # WiFi/network event management, WIFI_CHAN
│       ├── heap_monitor/             # Heap monitor module (system + mbedTLS)
│       ├── wifi_prov_over_ble/       # WiFi provisioning over BLE (subscribes WIFI_CHAN)
│       ├── app_memfault/             # Memfault application module
│       │   ├── core/                 # Upload, heartbeat, boot confirm, WIFI/BUTTON
│       │   ├── metrics/              # WiFi + stack + heap metrics
│       │   ├── ota/                  # OTA triggers (button 2, WiFi connect)
│       │   └── cdr/                  # nRF70 FW stats CDR
│       ├── app_https_client/         # App HTTPS request test (WIFI_CHAN)
│       │   └── cert/                 # HTTPS root CA certificate
│       └── app_mqtt_client/          # App MQTT echo test (WIFI_CHAN)
│           └── cert/                 # MQTT broker CA certificate
├── sysbuild/                         # Multi-image (MCUboot, hci_ipc, app)
├── overlay-app-memfault-project-key.conf  # Memfault key (create from template, git-ignored)
├── pm_static_*.yml                   # Flash partition layout
├── PRD.md                            # Product requirements
├── LICENSE                           # Nordic 5-Clause
└── README.md
```

---

## Building Firmware

> **Note**: `prj.conf` uses a placeholder project key. For production, supply your real key via `-DEXTRA_CONF_FILE="overlay-app-memfault-project-key.conf"`.

### Standard Build

**Includes**: WiFi provisioning over BLE + HTTPS client + MQTT client + Memfault + nRF70 CDR

```bash
west build -b nrf7002dk/nrf5340/cpuapp -p -- \
  -DEXTRA_CONF_FILE="overlay-app-memfault-project-key.conf"
west flash --erase
```

**Features enabled**:
- ✅ WiFi provisioning over BLE
- ✅ Memfault crash reporting, metrics, OTA
- ✅ Heap monitor module via `CONFIG_HEAPS_MONITOR`
- ✅ nRF70 firmware statistics CDR (Button 1)
- ✅ WiFi vendor detection (AP OUI lookup)
- ✅ Periodic HTTPS HEAD requests to `example.com` (every 60 s, configurable via `CONFIG_APP_HTTPS_REQUEST_INTERVAL_SEC`)
- ✅ Metrics: `app_https_req_total_count`, `app_https_req_fail_count`
- ✅ TLS-secured MQTT connection to `test.mosquitto.org:8883`
- ✅ Publishes messages and subscribes to same topic (echo test)
- ✅ Metrics: `app_mqtt_echo_total_count`, `app_mqtt_echo_fail_count`

---

## Flash Memory Layout

This project uses a custom partition layout optimized for Memfault operation and Wi-Fi connectivity.

## App Core Memory

| Memory Region | Used Size | Region Size | Usage |
|---------------|-----------|-------------|-------|
| FLASH | 889460 B | 941568 B | 94.47% |
| RAM | 421912 B | 512 KB | 80.47% |

### Internal Flash (1MB)

```
┌────────────────────────────────────────────────────────────────┐
│                    nRF5340 Internal Flash (1MB)                │
├────────────────────────────────────────────────────────────────┤
│ 0x00000 ┌─────────────────────────────────────┐                │
│         │            mcuboot                  │ 40KB           │
│         │          (Bootloader)               │ (0xA000)       │
│ 0x0A000 ├─────────────────────────────────────┤                │
│         │        mcuboot_pad                  │ 512B           │
│ 0x0A200 ├─────────────────────────────────────┤ (0x200)        │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│         │              app                    │ 911KB          │
│         │        (Main Application)           │ (0xE3E00)      │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│ 0xEE000 ├─────────────────────────────────────┤                │
│         │       settings_storage              │ 8KB            │
│         │    (Wi-Fi Credentials & Settings)   │ (0x2000)       │
│ 0xF0000 ├─────────────────────────────────────┤                │
│         │                                     │                │
│         │       memfault_storage              │ 64KB           │
│         │     (Crash Dumps — coredumps)       │ (0x10000)      │
│         │                                     │                │
│ 0x100000└─────────────────────────────────────┘                │
└────────────────────────────────────────────────────────────────┘
```

### External Flash (8MB - MX25R64)

```
┌────────────────────────────────────────────────────────────────┐
│                  MX25R64 External Flash (8MB)                  │
├────────────────────────────────────────────────────────────────┤
│ 0x00000 ┌─────────────────────────────────────┐                │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│         │        mcuboot_secondary            │ 911KB          │
│         │      (App Update Slot)              │ (0xE4000)      │
│         │                                     │                │
│         │                                     │                │
│ 0xE4000 ├─────────────────────────────────────┤                │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│         │                                     │                │
│         │         external_flash              │ 7.1MB+         │
│         │         (Reserved, Unused)          │ (0x71C000)     │
│         │                                     │                │
│         │    ⚠️  Currently not used by the    │                │
│         │       sample application.           │                │
│         │                                     │                │
│         │    See "External Flash Usage" note  │                │
│         │    below for implementation guide.  │                │
│         │                                     │                │
│ 0x800000└─────────────────────────────────────┘                │
└────────────────────────────────────────────────────────────────┘
```

### Flash Partition Usage

#### `memfault_storage` (Internal Flash)
The 64KB partition stores crash coredumps. **Must** be in internal flash for:
- Power-fail safety (survives brownouts)
- Boot-time access (before external flash init)
- Minimal dependencies (no SPI/QSPI driver needed)

#### `external_flash` (External Flash - Unused)
> ⚠️ The 7.1MB partition is **reserved but currently unused**.

### SRAM (512KB)

```
┌──────────────────────────────────────────────────────────────────┐
│                    nRF5340 SRAM (512KB)                          │
├──────────────────────────────────────────────────────────────────┤
│ 0x20000000 ┌───────────────────────────────────────────────-───┐ │
│            │                                                   │ │
│            │              Application SRAM                     │ │
│            │           (Stack, Heap, Variables)                │ │
│            │                   512KB                           │ │
│            │               (0x80000)                           │ │
│ 0x20080000 └───────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

### Network Core Memory

The nRF5340 network core runs the BLE controller (`hci_ipc`):

| Memory | Used | Total | Usage |
|--------|------|-------|-------|
| FLASH | 151.9 KB | 256 KB | 57.95% |
| RAM | 38.9 KB | 64 KB | 59.35% |

> **Note:** Largest RAM consumer is BLE Controller Memory Pool (15.9 KB). Reduce via `CONFIG_BT_MAX_CONN`.

---

## Basic Memfault Cloud Usage

### Device Onboarding

1. **Upload symbol file** after building:
   - File: `build/memfault-nrf7002dk/zephyr/zephyr.elf`
   - Dashboard: **Fleet** → **Symbol Files** → Upload

2. **Monitor device**:
   - **Timeline**: Real-time event stream
   - **Metrics**: Heartbeat data (every 15 min)
   - **Issues**: Crashes and errors

3. **Enable Developer Mode** for faster uploads:
   - Default: 900s (15 min)
   - Dev Mode: ~60s

### Collected Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `wifi_rssi` | Gauge | Signal strength (dBm) |
| `wifi_sta_*` | Gauge | Channel, beacon interval, DTIM, TWT |
| `wifi_ap_oui_vendor` | String | AP vendor (Cisco, Apple, ASUS, etc.) |
| `ncs_system_heap_*` | Gauge | System heap total, current usage, and peak usage |
| `ncs_mbedtls_heap_*` | Gauge | mbedTLS heap total, current usage, and peak usage |
| `stack_free_*` | Gauge | Per-thread stack usage |

### OTA Updates

1. **Update version** in `prj.conf`:
   ```properties
   CONFIG_MEMFAULT_NCS_FW_VERSION="4.1.0"
   ```

2. **Build and upload**:
   ```bash
   west build -b nrf7002dk/nrf5340/cpuapp -p
   ```
   - Upload `zephyr.elf` (symbols)
   - Upload `zephyr.signed.bin` (OTA payload)

3. **Create release**:
   - Dashboard: **Fleet** → **OTA Releases**
   - Activate for device cohort

4. **Trigger update**:
   - **Auto**: On WiFi connect + every 60 min
   - **Manual**: Button 2 short press

---

## Advanced Topics

### nRF70 WiFi Firmware Statistics (CDR)

The nRF70 firmware statistics feature is **enabled by default** and collects WiFi diagnostics via Memfault's Custom Data Recording.

#### Usage

**Manual Collection**:
- Press **Button 1** (short press) to collect and upload nRF70 stats

**Programmatic Collection**:
```c
#include "mflt_nrf70_fw_stats_cdr.h"

void on_wifi_failure(void) {
    int err = mflt_nrf70_fw_stats_cdr_collect();
    if (err == 0) {
        memfault_zephyr_port_post_data();
    }
}
```

#### Recommended Collection Events

| Event | When to Collect |
|-------|------------------|
| WiFi Connection Lost | Before reconnection attempt |
| DHCP/DNS Failure | After timeout |
| Low RSSI | When < -80 dBm |
| Cloud Unreachable | After connection timeout |

#### Parsing Statistics

Download CDR blob from Memfault and parse:

```bash
python3 script/nrf70_fw_stats_parser.py \
  /opt/nordic/ncs/v3.2.1/nrf/modules/lib/nrf_wifi/fw_if/umac_if/inc/fw/host_rpu_sys_if.h \
  ~/Downloads/F4CE36006EB1_nrf70-fw-stats_20251128-111955.bin
```

**Output includes**: PHY stats (RSSI, CRC), LMAC stats (TX/RX counters), UMAC stats (events, packets)

#### CDR Limitations

> ⚠️ **1 upload per device per 24 hours** by default. Contact Memfault support to increase limits for debugging.

### Custom Metrics

Add to `src/modules/app_memfault/config/memfault_metrics_heartbeat_config.def`:

```c
MEMFAULT_METRICS_KEY_DEFINE(custom_counter, kMemfaultMetricType_Unsigned)
```

Record in code:
```c
MEMFAULT_METRIC_SET_UNSIGNED(custom_counter, value);
```

### Heap Monitor Module

Heap monitoring is implemented as a reusable module in `src/modules/heap_monitor/`
and is enabled with `CONFIG_HEAPS_MONITOR=y`.

What it does:
- Automatically monitors the system heap when `CONFIG_HEAP_MEM_POOL_SIZE > 0`
- Automatically monitors the mbedTLS heap when `CONFIG_MBEDTLS_ENABLE_HEAP=y`
- Updates Memfault heartbeat metrics automatically when `CONFIG_APP_MEMFAULT_MODULE=y`
- Emits standardized logs for both heaps, for example:

```text
System Heap: used=51712/98304 (52%) blocks=n/a, peak=64752/98304 (65%), peak_blocks=n/a
mbedTLS Heap: used=19136/110592 (17%) blocks=49, peak=72684/110592 (65%), peak_blocks=197
```

Useful Kconfig options:
- `CONFIG_HEAPS_MONITOR`
- `CONFIG_HEAPS_MONITOR_WARN_PCT`
- `CONFIG_HEAPS_MONITOR_STEP_BYTES`
- `CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC`

### Partition Layout Customization

Edit `pm_static_nrf7002dk_nrf5340_cpuapp.yml`:
- Ensure `app` and `mcuboot_secondary` match sizes
- Keep `settings_storage` for WiFi credentials
- Rebuild with `-p` flag

---

## Troubleshooting

### No WiFi credentials after flash

Device advertises as `PV<MAC>` over BLE but never connects to WiFi.

**Fix**: Use the [nRF Wi-Fi Provisioner](https://www.nordicsemi.com/Products/Development-tools/nRF-Wi-Fi-Provisioner) app (Android/iOS) to provision credentials. Once provisioned, credentials are stored in the settings partition and survive reboots.

Alternatively, if the shell is enabled (`CONFIG_SHELL=y`):
```
uart:~$ wifi_cred add "MySSID" 3 "MyPassword"
uart:~$ kernel reboot cold
```

---

### Memfault upload fails / no data in dashboard

**Check**:
1. Verify the project key: `overlay-app-memfault-project-key.conf` must contain your real key from the Memfault dashboard.
2. Verify DNS: the device must reach `chunks-nrf.memfault.com:443`. Check firewall/router rules.
3. Check logs for `[memfault_core] DNS check: chunks-nrf.memfault.com resolved` — if missing, DNS is blocked.
4. Ensure the symbol file (`zephyr.elf`) has been uploaded for the current firmware version.

**DNS timeout log** (expected if DNS is temporarily slow — uploads still proceed after 300 s):
```
[memfault_core] DNS timeout after 300 seconds, continuing anyway
```

---

### OTA update not triggering

**Check**:
1. A release must be created and activated in the Memfault dashboard for your device's cohort.
2. Verify the firmware version in `prj.conf` (`CONFIG_MEMFAULT_NCS_FW_VERSION`) is lower than the release version.
3. Press **Button 2** (short) to force an immediate OTA check, or wait up to `CONFIG_MEMFAULT_OTA_CHECK_INTERVAL_MIN` minutes (default: 60).

---

### High flash usage warning

Flash is at ~94%. If a future build fails with a linker error:
1. Disable unused features in `prj.conf` (`CONFIG_APP_HTTPS_CLIENT_MODULE=n` or `CONFIG_APP_MQTT_CLIENT_MODULE=n`).
2. Disable `CONFIG_SHELL=n` (already off by default).
3. Reduce `CONFIG_LOG_BUFFER_SIZE`.

---

### Device crashes on boot after OTA

MCUboot will automatically roll back to the previous image if the new firmware does not call `boot_write_img_confirmed()`. The Memfault core module calls this on a successful boot. If rollback occurs repeatedly, check for early-boot crashes in the Memfault **Issues** tab.

---

## Resources

- **Memfault**: [Documentation](https://docs.memfault.com) | [Metrics API](https://docs.memfault.com/docs/mcu/metrics-api) | [OTA Guide](https://docs.memfault.com/docs/mcu/releases-integration-guide)
- **Nordic**: [NCS Docs](https://docs.nordicsemi.com) | [nRF7002DK Guide](https://docs.nordicsemi.com/category/hardware-development-kits) | [DevZone](https://devzone.nordicsemi.com)
- **Tools**: [nRF Wi-Fi Provisioner](https://www.nordicsemi.com/Products/Development-tools/nRF-Wi-Fi-Provisioner) | [nRF Connect Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop)

## License

Based on Nordic Semiconductor's Memfault sample (LicenseRef-Nordic-5-Clause).

## TODO

- Improve app_https_client and app_mqtt_client listener for network instead of wifi
- Memory optimization
- Add support for nRF54LM20DK+nRF7002EBII
- Solve firmware stuck issue
