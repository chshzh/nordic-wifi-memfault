# Flash Memory Layout - nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-05-14-14-23 |
| PRD Version | 2026-05-14-14-13 |
| Author | GitHub Copilot |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-15-22-02 | nRF54LM20DK: boot partition 56 KB → 64 KB; nRF7002DK: coredump backend changed from RAM-backed to flash-backed (custom) |
| 2026-05-14-14-23 | Added detailed flash/partition layout and PM-to-DTS migration compatibility notes |

---

## Flash Memory Layout

NCS v3.3+ Migration Note: As of NCS v3.3.0, this project has migrated from the legacy Partition Manager (PM) build-time system to DeviceTree fixed-partitions (DTS-driven). The layouts below reflect the DTS-based configuration deployed via overlay files.

### nRF7002DK (nRF5340 - 1 MB internal flash)

#### New Layout (DTS-based, NCS 3.3+)

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x00000` | `boot_partition` | 40 KB | Bootloader (MCUboot) |
| `0x0A000` | `slot0_partition` | 920 KB | Primary app image |
| `0xEE000` | `storage_partition` | 8 KB | WiFi credentials / NVS |
| `0xF0000` | `memfault_storage` | 64 KB | Crash coredumps (flash-backed, `CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM=y`) |

External flash - MX25R64 (8 MB):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `slot1_partition` | 920 KB | Secondary OTA slot |

#### Legacy Layout (Partition Manager, NCS 3.2 and earlier)

| Address | Partition | Size | Purpose | Notes |
|---------|-----------|------|---------|-------|
| `0x00000` | `mcuboot` | 40 KB | Bootloader | - |
| `0x0A000` | `mcuboot_pad` | 512 B | Image header padding | Removed in DTS |
| `0x0A200` | `app` | 912 KB | Main application | Now starts at `0x0A000` |
| `0xEE000` | `settings_storage` | 8 KB | WiFi credentials / NVS | - |
| `0xF0000` | `memfault_storage` | 64 KB | Crash coredumps | - |

Key difference: the 512-byte `mcuboot_pad` is eliminated, shifting app start earlier by 0x200 bytes.

---

### nRF54LM20DK + nRF7002EB II (nRF54LM20A - 1984 KB RRAM)

#### New Layout (DTS-based, NCS 3.3+)

Internal flash - RRAM (`cpuapp_rram`):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `boot_partition` | 64 KB | Bootloader (MCUboot) |
| `0x010000` | `slot0_partition` | 1804 KB | Primary app image |
| `0x1D3000` | `storage_partition` | 8 KB | WiFi credentials / NVS |
| `0x1D5000` | `memfault_coredump_partition` | 64 KB | Crash coredumps (RRAM-backed, `CONFIG_MEMFAULT_COREDUMP_STORAGE_RRAM=y`) |

External flash - MX25R6435F (8 MB via spi00):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `slot1_partition` | 1804 KB | Secondary OTA slot |

#### Legacy Layout (Partition Manager, NCS 3.2 and earlier)

Internal flash - RRAM (`flash_primary`):

| Address | Partition | Size | Purpose | Notes |
|---------|-----------|------|---------|-------|
| `0x000000` | `mcuboot` | 56 KB | Bootloader | - |
| `0x0E000` | `mcuboot_pad` | 2 KB | Image header padding | Removed in DTS |
| `0x0E800` | `app` | 1810 KB | Main application | Now starts at `0x0E000` |
| `0x1D3000` | `settings_storage` | 8 KB | WiFi credentials / NVS | - |
| `0x1D5000` | `memfault_coredump_partition` | 64 KB | Crash coredumps | - |

External flash - MX25R6435F (8 MB via spi00):

| Address | Partition | Size | Purpose | Notes |
|---------|-----------|------|---------|-------|
| `0x000000` | `mcuboot_secondary` | 1812 KB | OTA slot | Now 1804 KB in DTS |

---

### Migration Rationale

| Aspect | Partition Manager (v3.2-) | DeviceTree (v3.3+) | Reason |
|--------|---------------------------|--------------------|--------|
| Configuration | YAML (`pm_static_*.yml`) | DTS overlays (`.overlay` files) | DTS is Zephyr standard; PM is deprecated |
| Build system | Sysbuild (legacy PM subsystem) | Standard MCUboot DTS parsing | Simplifies toolchain; DTS-driven boot is universal |
| Pad bytes | Explicit `mcuboot_pad` node (512 B or 2 KB) | Eliminated; slot addresses aligned directly | Reduces complexity; recovers bytes for app |
| Coexistence | PM-based and DTS partitions mutually exclusive | DTS only | Cleaner migration path; single authoritative source |
| Firmware compatibility | Different builds use same address layout | Different address alignment per layout | OTA across PM<->DTS boundary is not supported |

Why the pad was removed:
- MCUboot image header handling is owned by bootloader, so explicit partition padding is no longer needed.
- DTS fixed-partitions are simpler and more flexible than PM derivation rules.

Important: Firmware compiled under PM (v3.2) cannot be OTA-updated to DTS (v3.3) and vice versa due to address shifts. Devices must be reflashed via hardware or a custom transition strategy.
