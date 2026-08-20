# Engineering Specs Overview — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-08-20-14-47 |
| PRD Version | 2026-08-20-14-47 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Status | Implemented |

> `Version` = this doc's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this doc tracks. The two normally **differ** — never set them equal.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-08-20-14-47 | Updated to PRD v2026-08-20-14-47 — **decoupled STA reconnect from BLE provisioning**: moved reconnect/boot-auto-connect ownership from `wifi_prov_over_ble.c` into the always-compiled `network` module, using a new `BLE_CHAN` Zbus channel so `network` can still avoid racing an active BLE provisioning session without a compile-time dependency on that module. Reconnect now works whether credentials are entered via BLE provisioning or `wifi cred shell`. Updated the Module Dependency Map with the new `wifi_prov_over_ble --BLE_CHAN--> network` edge. See `network-module.md` and `app-wifi-prov-ble-module.md` Changelogs for the full account. |
| 2026-08-20-14-10 | Updated to PRD v2026-08-20-14-10 — **removed all SoftAP scaffolding** (Kconfig options, `net_event_mgmt.c`/`wifi_utils.c` handlers). Closes Open Issue #2 (below) as "removed" rather than "complete". Module Dependency Map's `network-module.md` description updated to drop the "SoftAP groundwork" mention. |
| 2026-08-20-13-20 | **Zbus event redesign**: `NETWORK_CHAN` (`NETWORK_READY`/`NETWORK_NOT_READY`) is now the channel every connectivity-gated module subscribes to, replacing `WIFI_CHAN`'s misleadingly-named `WIFI_STA_CONNECTED` (which actually fired on IP assignment, not L2 association) and dead `WIFI_DNS_READY`. `WIFI_CHAN` is now L2-only with zero subscribers. Updated Module Dependency Map. See `network-module.md` Changelog for the full rationale and blast radius. |
| 2026-07-13-11-08 | Migrated from `pm/openspec/specs/*.md` and reverse-designed against current `src/` on the `ncs264` branch (NCS v2.6.4, dual-board, `network` module replacing `wifi` module, `heap_monitor` added) |
| 2026-07-13-12-22 | Updated to PRD v2026-07-13-12-22: added FR-102/FR-103 (log-state and nRF70 CDR persist/restore across power cycle, ported from `nordic-wifi-memfault-main`'s FR-007/FR-008) to `app-memfault-module.md` and `2-pm-partition.md`. Design only — not yet implemented. |
| 2026-07-13-13-31 | FR-102/FR-103 implemented and build-verified on nRF7002DK (FLASH 90.26%, RAM 98.75%). See `app-memfault-module.md` and `2-pm-partition.md` changelogs for implementation details and the FR-102 design deviation (drain-and-replay vs. raw ring-buffer copy). |
| 2026-07-24-11-30 | Updated to PRD v2026-07-24-11-30 (reliability hardening pass). Cross-cutting fixes distributed across module specs — see each file's own Changelog for detail: FR-102 keep-newest eviction fix (`app-memfault-module.md`); Wi-Fi reconnect backoff + L3 DHCP-bound watchdog (`network-module.md`, `app-wifi-prov-ble-module.md`); ported DNS-reliability fixes (`app-memfault-module.md`, `app-https-client-module.md`); `tcp_work` stack-overflow fix diagnosed via a symbolicated Memfault coredump trace (`3-memopt.md`). |
| 2026-07-24-14-09 | Updated to PRD v2026-07-24-14-09: added FR-104 (NTP time sync) — new [ntp-module.md](ntp-module.md), ported from `zego/bricks/ntp`, subscribing directly to the existing `WIFI_CHAN` instead of the brick's decoupled net-state channel. Also notes in `app-memfault-module.md` that FR-102's restore-time-timestamp limitation is resolved once FR-104 has completed its first sync. |
| 2026-07-24-14-41 | Updated to PRD v2026-07-24-14-41: two FR-104 fixes found during hardware testing — `ntp-module.md`'s `CMakeLists.txt` never actually linked the module into the app (`zephyr_library()` pattern vs. this app's `target_sources(app PRIVATE ...)` convention), and UART log timestamps now do switch to real Unix-epoch time post-sync via `log_set_timestamp_func()` (previously assumed impossible in this Zephyr version). See `ntp-module.md` changelog for detail. |
| 2026-07-24-14-47 | Updated to PRD v2026-07-24-14-47: UART log timestamps upgraded from raw epoch seconds to a calendar UTC string via a custom Zephyr log formatter (`CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP`). See `ntp-module.md` changelog for detail. |
| 2026-07-24-14-56 | Updated to PRD v2026-07-24-14-56: bug fix for a deferred-logging race that could briefly render bogus 1970 dates around the NTP sync transition, plus `[...] ` bracket formatting to match the original log style. See `ntp-module.md` changelog for detail. |
| 2026-08-19-15-05 | Updated to PRD v2026-08-19-15-00 ("Wi-Fi reconnect reliability, round 2"). Two independent reconnect-hang bugs fixed in `wifi_prov_over_ble.c` — see [app-wifi-prov-ble-module.md](app-wifi-prov-ble-module.md) Changelog (2026-08-18-18-40, 2026-08-18-22-15) for detail. New opt-in periodic nRF70 CDR collection (`CONFIG_NRF70_FW_STATS_CDR_PERIODIC_INTERVAL_SEC`) added; a companion periodic coredump-retry feature was implemented, found to be both redundant with Memfault's own periodic upload timer and the direct cause of a boot-time newlib-heap assertion loop on nRF7002DK (only ~2 KB RAM headroom on this target), then removed entirely — see [app-memfault-module.md](app-memfault-module.md) Changelog (2026-08-19-10-45 through 2026-08-19-13-00) for the full account, including the RAM-margin postmortem. Also tightened four periodic-interval Kconfig values (Memfault HTTP upload, Memfault heartbeat-force, HTTPS request, MQTT publish) from 900 s to 60 s in `prj.conf` for faster feedback during active development. |
| 2026-08-19-15-50 | Updated to PRD v2026-08-19-15-45 — **removed nRF54LM20DK + nRF7002EB II support project-wide.** No board definition for this SoC exists anywhere in this NCS v2.6.4 installation; this project's dual-board support was never actually buildable/verifiable in this environment (the persistent, previously-unresolved "BOARD_ROOT environment issue" noted since 2026-07-13 — see `app-memfault-module.md` Open Issues history). Target Board(s) updated to nRF7002DK-only across every spec file; FR-006 (dual-board support) removed from the PRD-to-Spec mapping below since it no longer applies; see [1-architecture.md](1-architecture.md) Changelog for the full list of deleted board/partition/sysbuild files. |
| 2026-08-20-11-01 | Updated to PRD v2026-08-20-10-59 — removed the `FR-201 SoftAP/P2P` row from the PRD-to-Spec Mapping (§4); the PRD dropped its P2 tier since SoftAP/P2P were already covered as unimplemented scaffolding in PRD §8 Out of Scope, so the separate FR-201 mapping row was redundant here too. |
| 2026-08-20-11-12 | **Closed stale Open Issues #1 and #4** (code-driven, not PRD-driven): both described `NETWORK_CHAN`/FR-102/FR-103 as still design-only, but confirmed in `core/memfault_core.c` that `memfault_network_listener` already subscribes to `NETWORK_CHAN` and is compiled in by default (`CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`/`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` default `y`) — these issues were simply never closed out after FR-102/FR-103 shipped. Updated the Module Dependency Map's `NETWORK_CHAN` edge to drop the stale `[planned]` tag. See matching fix in [1-architecture.md](1-architecture.md) Changelog. |

---

## 1. Purpose

This document is the entry point for the engineering specs of `nordic-wifi-memfault`.
It maps product requirements to spec files and captures top-level design decisions.

For the product requirements that drive this design, see [../pm-prd/PRD.md](../pm-prd/PRD.md).

---

## 2. Spec Index

| Spec file | Covers | PRD sections |
|-----------|--------|--------------|
| [1-architecture.md](1-architecture.md) | System overview, module map, Zbus channels, boot sequence, memory budget | All |
| [2-pm-partition.md](2-pm-partition.md) | Flash partition layout per board (legacy Partition Manager, NCS v2.6.4) | FR-006, NFR flash/OTA |
| [3-memopt.md](3-memopt.md) | Memory optimization — stack watermarks, heap budget, headroom | NFR memory |
| [button-module.md](button-module.md) | Button SMF state machine, press actions | FR-003, FR-004 |
| [network-module.md](network-module.md) | Wi-Fi STA connectivity, L2/L3 event management | FR-001, FR-006 |
| [app-wifi-prov-ble-module.md](app-wifi-prov-ble-module.md) | Wi-Fi credential provisioning via BLE | FR-001 |
| [app-memfault-module.md](app-memfault-module.md) | Memfault core (upload/DNS-wait), metrics, OTA triggers, nRF70 stats CDR, log-state/CDR persist-restore | FR-002, FR-003, FR-004, FR-101, FR-102, FR-103 |
| [app-https-client-module.md](app-https-client-module.md) | Always-on periodic HTTPS client | FR-005 |
| [app-mqtt-client-module.md](app-mqtt-client-module.md) | Always-on TLS MQTT echo client | FR-005 |
| [heap-monitor-module.md](heap-monitor-module.md) | System/mbedTLS heap tracking, Memfault heap metrics | FR-007 |
| [ntp-module.md](ntp-module.md) | SNTP time sync, real-world log/Memfault timestamps | FR-104 |

---

## 3. Architecture Summary

**Pattern**: SMF + Zbus modular

**Key design decisions:**

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture pattern | SMF + Zbus | Decoupled modules; Zbus (`BUTTON_CHAN`, `WIFI_CHAN`, `NETWORK_CHAN`, `BLE_CHAN`) is the only inter-module channel — `WIFI_CHAN` is L2-only, `NETWORK_CHAN` is the IP-layer readiness signal that connectivity-gated modules actually subscribe to |
| Wi-Fi/network module split | `network/net_event_mgmt.c` (L2/L3 event handling + STA reconnect ownership) + `network/wifi_utils.c` (helpers: mode/channel/credential checks) | Legacy `wifi/wifi.c` was split for clarity; STA-only — SoftAP scaffolding was removed entirely (2026-08-20-14-10); reconnect logic moved here from `wifi_prov_over_ble` (2026-08-20-14-47) so it works regardless of BLE provisioning being enabled |
| Configuration | `prj.conf` (system-level + module enable flags) + per-module `Kconfig.<name>` and `Kconfig.defaults` | Each module owns its tuning defaults; `prj.conf` only overrides project-specific values and Kconfig `choice` symbols |
| Credentials | Wi-Fi: `wifi_credentials` NVS backend. Memfault: git-ignored `overlay-app-memfault-project-info.conf` | Never in source control |
| Partitioning | Legacy Partition Manager (`pm_static_<board>.yml`) | NCS v2.6.4 predates the DTS fixed-partitions migration (NCS v3.3+) |
| Single-board target | nRF7002DK only | nRF54LM20DK + nRF7002EB II removed 2026-08-19 — no board definition for that SoC exists in NCS v2.6.4 |

---

## 4. PRD-to-Spec Mapping

| PRD requirement | Spec file | Status |
|----------------|-----------|--------|
| FR-001 Wi-Fi STA connect + provisioning | network-module.md, app-wifi-prov-ble-module.md | Specified |
| FR-002 Memfault upload on connect | app-memfault-module.md | Specified |
| FR-003 Button 1 heartbeat / stack-overflow demo | button-module.md, app-memfault-module.md | Specified |
| FR-004 Button 2 OTA / division-by-zero demo | button-module.md, app-memfault-module.md | Specified |
| FR-005 Always-on HTTPS/MQTT clients | app-https-client-module.md, app-mqtt-client-module.md | Specified |
| ~~FR-006 Dual-board support~~ | — | Removed 2026-08-19 — nRF54LM20DK dropped (no board definition in NCS v2.6.4); project is single-board (nRF7002DK) |
| FR-007 Heap monitor → Memfault metrics | heap-monitor-module.md | Specified |
| FR-101 nRF70 stats CDR | app-memfault-module.md | Specified |
| FR-102 Log-state persist/restore across power cycle | app-memfault-module.md, 2-pm-partition.md | Implemented |
| FR-103 nRF70 CDR persist/restore across power cycle | app-memfault-module.md, 2-pm-partition.md | Implemented |
| FR-104 NTP time sync | ntp-module.md | Implemented |

---

## 5. Module Dependency Map

```
network        ──Zbus(NETWORK_CHAN)─▶ app_memfault (core) [also FR-102/FR-103, memfault_network_listener]
network        ──Zbus(NETWORK_CHAN)─▶ app_memfault (ota_triggers)
network        ──Zbus(NETWORK_CHAN)─▶ app_wifi_prov_over_ble
network        ──Zbus(NETWORK_CHAN)─▶ app_https_client
network        ──Zbus(NETWORK_CHAN)─▶ app_mqtt_client
network        ──Zbus(NETWORK_CHAN)─▶ ntp
network        ──Zbus(WIFI_CHAN)────▶ (no subscribers — L2-only, reserved for future consumers)
app_wifi_prov_over_ble ──Zbus(BLE_CHAN)──▶ network [defers STA reconnect while a BLE client is connected]
button         ──Zbus(BUTTON_CHAN)──▶ app_memfault (core)
button         ──Zbus(BUTTON_CHAN)──▶ app_memfault (ota_triggers)
heap_monitor   ──direct call────────▶ Memfault metrics API (no Zbus)
```

> For the full Zbus channel table, see [1-architecture.md](1-architecture.md).

---

## 6. Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| ~~1~~ | ~~`NETWORK_CHAN` currently has no subscribers — `app_memfault` will become its first subscriber once FR-102/FR-103 (log-state/CDR persist-restore) are implemented~~ — **Resolved**: `app_memfault` subscribes via `memfault_network_listener` (`core/memfault_core.c`), compiled in by default since `CONFIG_APP_MEMFAULT_LOG_STATE_RESTORE`/`CONFIG_APP_MEMFAULT_CDR_STATE_RESTORE` default `y` | — | — |
| ~~2~~ | ~~SoftAP/P2P Kconfig and event-handler scaffolding present in `network` module but unused; decide whether to finish or remove~~ — **Resolved**: removed entirely (2026-08-20-14-10); this sample only uses Wi-Fi STA mode | — | — |
| 3 | 24-hour soak test and full ZView-based memory measurement pass not yet run on either board (see [3-memopt.md](3-memopt.md)) | — | Next hardware validation pass |
| ~~4~~ | ~~FR-102/FR-103 design ports `nordic-wifi-memfault-main`'s log-state/CDR restore feature; must verify the bundled NCS v2.6.4 Memfault SDK exposes `memfault_log_get_state()`/`MEMFAULT_LOG_RESTORE_STATE` before implementation~~ — **Resolved**: confirmed those APIs do **not** exist in the bundled Memfault SDK v1.6.0; implemented via a drain-and-replay approach instead (see `app-memfault-module.md` Open Issues, 2026-07-13-13-31 changelog entry) | — | — |

*(Changelog is maintained at the top of this document.)*
