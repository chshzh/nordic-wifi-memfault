# System Architecture Specification — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-08-19-15-35 |
| PRD Version | 2026-08-19-15-00 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Status | Implemented |

> `Version` = this doc's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this doc tracks. The two normally **differ** — never set them equal.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Migrated from `pm/openspec/specs/architecture.md`; updated for current code: `wifi` module renamed/split into `network` module (`net_event_mgmt.c` + `wifi_utils.c`), added `heap_monitor` module, dropped the 1-second delayed boot-connect (network module now connects without artificial delay), added dual-board module map and NCS v2.6.4 Partition Manager note |
| 2026-08-19-15-35 | **Removed nRF54LM20DK + nRF7002EB II support project-wide.** This board has no board definition anywhere in this NCS v2.6.4 installation (confirmed absent from `zephyr/boards`, `nrf/boards`, and `modules/hal/nordic`) — the nRF54L series was introduced in a later NCS release, and this project's app-level `boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf`/`.overlay` files only ever *overrode* Kconfig/DTS on top of a base board definition that was never actually present in this tree. This matches the persistent, previously-unresolved "BOARD_ROOT environment issue" noted in this project's Open Issues since 2026-07-13 (see `app-memfault-module.md` history) — the board build was never actually verified working in this exact environment. Deleted: both `boards/nrf54lm20dk_*` files, `pm_static_nrf54lm20dk_*.yml`, both `sysbuild/mcuboot/boards/nrf54lm20dk_*` files. Removed board-conditional Kconfig defaults for it in `Kconfig.sysbuild` and `app_memfault/Kconfig.defaults`. Project is now nRF7002DK-only; single-board module map, memory budget, and Zbus channel design are otherwise unchanged. |

---

## Overview

**Application architecture**: nordic-wifi-memfault application code uses an **SMF + Zbus modular** architecture.
Each application feature lives in its own module under `src/modules/`.
All inter-module communication is exclusively through Zbus channels (`BUTTON_CHAN`, `WIFI_CHAN`, `NETWORK_CHAN`).
Application modules initialize through `SYS_INIT` (priority-ordered) or `K_THREAD_DEFINE` at boot time.

> **Scope note**: The architecture pattern describes **application code only**.
> External libraries (Memfault SDK, Wi-Fi driver/WPA supplicant, BLE stack) run in their own
> internal threads and are not subject to this architecture. Application-level wrapper
> modules (`app_memfault/`, `app_https_client/`, `app_mqtt_client/`) provide the interface
> boundary — calling library APIs, implementing required callbacks, and integrating library
> events into the app's Zbus message flow.

**Changes vs. the pre-migration (`pm/openspec`) design**: the former `wifi/wifi.c` module was
split into `network/net_event_mgmt.c` (L2/L3 net_mgmt event handling, `WIFI_CHAN` +
`NETWORK_CHAN` publishing, SoftAP event-handler groundwork) and `network/wifi_utils.c`
(credential/mode/channel helper functions). A new `heap_monitor` module was added to track
system-heap and mbedTLS-heap usage and feed it into Memfault metrics. The project was ported
from NCS v3.2.4 to v2.6.4, which uses the legacy Partition Manager instead of DTS
fixed-partitions and requires the explicit `--sysbuild` build flag.

---

## Module Map

```
src/
├── main.c                        ← startup banner, enabled-module log, no feature logic
└── modules/
    ├── messages.h                ← all Zbus message structs (shared)
    │
    │   ── Application modules (SMF+Zbus) ──
    ├── button/                   ← SMF button state machine, BUTTON_CHAN publisher
    ├── network/                  ← Wi-Fi L2/L3 event mgmt, WIFI_CHAN/NETWORK_CHAN publisher
    ├── heap_monitor/              ← periodic + peak heap sampling, no Zbus (direct Memfault metric calls)
    │
    │   ── Library wrapper modules ──
    ├── app_memfault/              ← wraps Memfault SDK
    │   ├── core/                  ← boot confirm, DNS-wait upload thread, button/wifi listeners
    │   ├── metrics/                ← wifi_metrics.c, stack_metrics.c (Memfault heartbeat data collectors)
    │   ├── ota/                    ← ota_triggers.c (button/connect/periodic OTA check thread)
    │   ├── cdr/                    ← nrf70_fw_stats_cdr.c (nRF70 PHY/LMAC/UMAC stats CDR)
    │   └── config/                 ← memfault_metrics_heartbeat_config.def
    ├── app_https_client/          ← wraps Zephyr HTTP client; periodic HEAD requests
    ├── app_mqtt_client/           ← wraps mqtt_helper library; TLS echo test
    └── wifi_prov_over_ble/        ← wraps BLE + wifi_provisioning service
```

---

## Zbus Channels

| Channel | Message Type | Publisher | Subscribers | Direction |
|---------|-------------|-----------|-------------|-----------|
| `BUTTON_CHAN` | `struct button_msg` | `button` | `app_memfault` (core listener), `app_memfault` (ota_triggers listener) | runtime |
| `WIFI_CHAN` | `struct wifi_msg` | `network` (`net_event_mgmt.c`) | `app_memfault` (core + ota_triggers), `wifi_prov_over_ble`, `app_https_client`, `app_mqtt_client` | runtime |
| `NETWORK_CHAN` | `struct network_msg` | `network` (`net_event_mgmt.c`) | none currently (reserved for future IP-layer-readiness consumers) | runtime |

### Message Definitions (`src/modules/messages.h`)

```c
/* Button messages */
enum button_msg_type {
	BUTTON_PRESSED,
	BUTTON_RELEASED,
};

struct button_msg {
	enum button_msg_type type;
	uint8_t button_number;   /* 1-4 */
	uint32_t duration_ms;
	uint32_t press_count;
	uint32_t timestamp;
};

#define BUTTON_LONG_PRESS_THRESHOLD_MS CONFIG_BUTTON_LONG_PRESS_MS

/* WiFi messages (STA mode) */
enum wifi_msg_type {
	WIFI_STA_CONNECTED,
	WIFI_STA_DISCONNECTED,
	WIFI_DNS_READY,
	WIFI_ERROR,
};

struct wifi_msg {
	enum wifi_msg_type type;
	int32_t rssi;
	int error_code;
};

/* Network ready messages (DHCP/IP) */
enum network_msg_type {
	NETWORK_READY,
	NETWORK_NOT_READY,
};

struct network_msg {
	enum network_msg_type type;
	bool ready;
};
```

> `BUTTON_LONG_PRESS_THRESHOLD_MS` is now correctly wired to `CONFIG_BUTTON_LONG_PRESS_MS`
> (fixed vs. the dead-Kconfig finding W-07 in the legacy `pm/QA.md` report).

---

## External Libraries

| Library | NCS Kconfig | Internal threads | App wrapper module |
|---------|-------------|------------------|--------------------|
| Memfault SDK | `CONFIG_MEMFAULT=y` (via `select` from `CONFIG_APP_MEMFAULT_MODULE`) | HTTP upload / FOTA download threads managed internally | `app_memfault/` |
| Wi-Fi driver + WPA supplicant (`WPA_SUPP`, nRF wrapper) | `CONFIG_WIFI_NRF700X=y`, `CONFIG_WPA_SUPP=y` | `wpa_supplicant` main + workqueue threads | `network/` |
| Bluetooth stack + Wi-Fi Provisioning Service | `CONFIG_BT=y`, `CONFIG_BT_WIFI_PROV=y` | BLE host + `adv_daemon` work queue (`wifi_prov_over_ble.c`) | `wifi_prov_over_ble/` |
| MQTT helper (`mqtt_helper`) | `CONFIG_MQTT_HELPER=y` (via `select`) | Internal MQTT socket/keepalive handling | `app_mqtt_client/` |

---

## Boot Sequence

| Priority / mechanism | Module | Init call | UART marker |
|----------|--------|---------------|-------------|
| `SYS_INIT(APPLICATION, CONFIG_WIFI_MODULE_INIT_PRIORITY=90)` | `network` | `init_network_events` | `[net_event_mgmt] ...` (interface/event registration) |
| `SYS_INIT(APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY)` | `button` | `button_module_init` | `Button module initialized` |
| `SYS_INIT(APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY)` | `app_memfault` (core) | `memfault_core_init` | `Memfault core init` |
| BLE stack init inside module | `wifi_prov_over_ble` | `bt_enable` + advertising start | (BLE provisioning logs) |
| `K_THREAD_DEFINE` at link time | `app_memfault` (core upload thread) | `upload_thread_fn` | `Connected to network` / `Waiting for DNS resolver...` |
| `K_THREAD_DEFINE` at link time | `app_memfault` (ota_triggers) | `mflt_ota_triggers_thread` | `Memfault OTA trigger thread started` |
| `main()` | — | prints board/version/MAC/enabled-module banner | `==============================================` |

> No `SYS_INIT` in this codebase uses `K_FOREVER`; the Memfault upload and OTA-trigger
> threads instead block on `k_sem_take()` with either `K_FOREVER` (upload — acceptable
> because it is a dedicated always-running thread, not a boot-blocking `SYS_INIT`) or a
> bounded periodic timeout (`OTA_CHECK_INTERVAL`, OTA triggers).

---

## Memory Budget

> Values below are the last known-good estimates from the pre-migration `pm/openspec/specs/architecture.md`
> (nRF7002DK, NCS v3.2.4 build). They have **not** been re-measured on NCS v2.6.4. Treat as
> directional only until `chsh-sk-ncs-3.3-memopt` produces a fresh measurement pass — see
> [3-memopt.md](3-memopt.md) Open Issues.

| Module | Flash (KB) | RAM (KB) | Notes |
|--------|-----------|---------|-------|
| Base (network + app_memfault) | ~750 | ~380 | STA connectivity + Memfault core/metrics/OTA |
| `wifi_prov_over_ble` | +45 | +20 | BLE stack + provisioning service |
| `app_https_client` | +30 | +8 | |
| `app_mqtt_client` | +25 | +12 | |
| nRF70 FW Stats CDR | +15 | +4 | part of `app_memfault/cdr` |
| `heap_monitor` | ~+2 | ~+1 | new since migration; not yet measured |
| **Total (all features, directional)** | **~865** | **~424** | Needs re-measurement — see [3-memopt.md](3-memopt.md) |

---

## Test Points

| Stage | UART log expected | Pass condition |
|-------|-------------------|----------------|
| Boot | `Board:   <board name>` then `==============================================` | Always |
| Module init | `Button module initialized`, `Memfault core init` | Per module |
| Wi-Fi connect | `[WiFi] WiFi is connected!` | STA association succeeds |
| Memfault upload | `Sending already captured data to Memfault` | After WIFI_STA_CONNECTED + DNS ready |
| OTA check | `Starting Memfault OTA check (<context>)` | Button 2 short press, connect, or periodic timer |

---

*(Changelog is maintained at the top of this document.)*
