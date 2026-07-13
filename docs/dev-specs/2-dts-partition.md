# Flash Memory Layout — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-07-13-11-08 |
| PRD Version | 2026-07-13-11-07 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB II |
| Status | Implemented |

> `Version` = this spec's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this spec tracks.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Migrated from legacy docs; layout taken from `pm_static_nrf7002dk_nrf5340_cpuapp.yml` and `pm_static_nrf54lm20dk_nrf54lm20a_cpuapp.yml` (both current on `ncs264` branch) |

---

## Overview

**This project stays on the legacy Zephyr Partition Manager (PM)**, not the DTS
fixed-partitions scheme — NCS v2.6.4 (the SDK version pinned for this `ncs264` branch)
predates the DTS-based partitioning migration that NCS v3.3+ uses. Layouts below reflect
`pm_static_<board>.yml` at the project root plus `sysbuild/mcuboot/` for the bootloader image.

`sysbuild.conf` uses the default Partition Manager path (no `SB_CONFIG_PARTITION_MANAGER=n`
override); PM computes addresses from `pm_static_*.yml` plus each image's own partition
requests.

---

## Flash Memory Layout

### nRF7002DK (nRF5340 — cpuapp, internal flash_primary; MX25R64 external flash)

#### Internal Flash (`flash_primary`)

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x00000` | `mcuboot` | 40 KB (`0xa000`) | Bootloader (MCUboot) |
| `0x0a000` | `mcuboot_pad` | 512 B (`0x200`) | MCUboot image header padding |
| `0x0a200` | `app` (`mcuboot_primary_app`) | 935.5 KB (`0xe3e00`) | Primary app image |
| `0xee000` | `settings_storage` | 8 KB (`0x2000`) | NVS — Wi-Fi credentials and small persistent app state |
| `0xf0000` | `memfault_storage` | 64 KB (`0x10000`) | Crash coredumps (internal-flash backend) |

#### External Flash (`MX25R64` — 8 MB)

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `mcuboot_secondary` | 935.5 KB (`0xe4000`) | Secondary OTA slot (matches `mcuboot_primary` span) |
| `0xe4000` | `external_flash` | ~7.1 MB (`0x71c000`) | Unused / reserved |

#### SRAM (`sram_primary`)

| Address | Region | Size | Purpose |
|---------|--------|------|---------|
| `0x20000000` | `sram_primary` | 448 KB (`0x70000`) | Application core RAM |
| `0x20070000` | `rpmsg_nrf53_sram` | 64 KB (`0x10000`) | IPC shared memory with network core (BLE `hci_ipc`) |

---

### nRF54LM20DK + nRF7002EB II (nRF54LM20A — RRAM `flash_primary`; MX25R6435F external flash)

#### Internal RRAM (`flash_primary` — 1984 KB / `0x1FD000` total)

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `mcuboot` | 56 KB (`0xE000`) | Bootloader |
| `0x00E000` | `mcuboot_pad` | 2 KB (`0x800`) | MCUboot image header padding |
| `0x00E800` | `app` (`mcuboot_primary_app`) | 1906 KB (`0x1DC800`) | Primary app image |
| `0x1EB000` | `settings_storage` | 8 KB (`0x2000`) | NVS — Wi-Fi credentials and small persistent app state |
| `0x1ED000` | `memfault_coredump_partition` | 64 KB (`0x10000`) | Crash coredumps (RRAM backend) |

> Total internal allocation is an exact fit: `0x1FD000` = 1984 KB.

#### External Flash (`MX25R6435F` — 8 MB)

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `mcuboot_secondary` | 1908 KB (`0x1DD000`) | Secondary OTA slot (matches `mcuboot_primary` span) |
| `0x1DD000` | `external_flash` | ~6.1 MB (`0x623000`) | Unused / reserved |

---

## Storage Partition Capacity Notes

The `settings_storage` partition (8 KB on both boards) is shared by the Zephyr settings
subsystem and all modules writing to NVS:

| Consumer | Estimated size | Notes |
|----------|---------------|-------|
| Wi-Fi credentials (SSID + PSK, `wifi_credentials`) | ~256 B | Per stored AP |
| MCUboot image-confirmed flag | negligible | Managed by MCUboot/Zephyr |
| **Total** | **~1 KB** | Well below 8 KB; no headroom concern currently |

---

## DTS Overlay Checklist (adapted for Partition Manager)

For each board, ensure the following exist and are consistent:

- [x] `pm_static_<board>.yml` at project root — defines `mcuboot`, `mcuboot_pad`, `app`, `settings_storage`, coredump partition, `mcuboot_secondary`, `external_flash`
- [x] `sysbuild/mcuboot/` — MCUboot image config picks up the same static partition map (shared `pm_static_*.yml`, not a separate overlay, under legacy PM)
- [x] `boards/<board>.conf` / `boards/<board>.overlay` — board-specific Kconfig and DTS overlay (e.g. shield selection for nRF54LM20DK)
- [x] `mcuboot_primary_app` (internal) size == `mcuboot_secondary` (external) size on both boards (MCUboot requirement) — verified: nRF7002DK `0xe3e00` vs `0xe4000` span incl. pad; nRF54LM20DK `0x1DC800` vs `0x1DD000` span incl. pad
- [x] Total internal flash/RRAM allocation ≤ SoC capacity — nRF54LM20DK: `0x1FD000` exact fit; nRF7002DK: within nRF5340 1 MB `flash_primary`

---

## Related Specs

- [1-architecture.md](1-architecture.md) — memory budget and SoC selection
- [3-memopt.md](3-memopt.md) — RAM budget and headroom tracking
