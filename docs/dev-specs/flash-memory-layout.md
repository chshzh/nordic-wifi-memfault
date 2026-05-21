# Flash Memory Layout - nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-05-21-10-01 |
| PRD Version | 2026-05-14-14-13 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-18-15-20 | Switched restore-path design to compact internal settings storage (no dedicated external restore partition) |
| 2026-05-21-10-01 | Added `mflt_cdr_state_partition` (8 KB) to external flash on both boards for FR-008 CDR flash persist/restore |
| 2026-05-18-14-45 | Explored larger external restore storage design (later superseded by compact internal settings-only approach) |
| 2026-05-18-14-05 | Clarified settings-storage use for disconnect diagnostics restore and noted 8 KB capacity limits for raw log snapshots |
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
| `0xEE000` | `storage_partition` | 8 KB | WiFi credentials / NVS; small persistent app state |
| `0xF0000` | `memfault_storage` | 64 KB | Crash coredumps (flash-backed, `CONFIG_MEMFAULT_COREDUMP_STORAGE_CUSTOM=y`) |

External flash - MX25R64 (8 MB):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `slot1_partition` | 920 KB | Secondary OTA slot |
| `0x0E4000` | `mflt_log_state_partition` | 8 KB | Memfault disconnect-time log-state blob (`CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`) |
| `0x0E6000` | `mflt_cdr_state_partition` | 8 KB | Memfault disconnect-time CDR blob (`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`) |

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
| `0x1D3000` | `storage_partition` | 8 KB | WiFi credentials / NVS; small persistent app state |
| `0x1D5000` | `memfault_coredump_partition` | 64 KB | Crash coredumps (RRAM-backed, `CONFIG_MEMFAULT_COREDUMP_STORAGE_RRAM=y`) |

External flash - MX25R6435F (8 MB via spi00):

| Address | Partition | Size | Purpose |
|---------|-----------|------|---------|
| `0x000000` | `slot1_partition` | 1804 KB | Secondary OTA slot |
| `0x1C3000` | `mflt_log_state_partition` | 8 KB | Memfault disconnect-time log-state blob (`CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`) |
| `0x1C5000` | `mflt_cdr_state_partition` | 8 KB | Memfault disconnect-time CDR blob (`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE`) |

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

### Settings Storage Capacity Note

The `storage_partition` / `settings_storage` area is only 8 KB on both boards and is shared by Zephyr settings data.

- It is enough for compact application state such as Wi-Fi credentials and small restore metadata.
- It is not enough for a full raw disconnect-diagnostics snapshot if the persisted payload is sized to a full 8 KB capture window, because the blob also needs format/header overhead and settings backend space.
- For the current option-2 design, Memfault log-state restore uses compact settings storage only with a strict size cap (default 3072 bytes payload) to protect shared settings capacity.
- Board scope: enabled on nRF54LM20DK; disabled on nRF7002DK due flash headroom constraints.
