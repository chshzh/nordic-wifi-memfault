# Engineering Specs Overview — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-07-13-11-08 |
| PRD Version | 2026-07-13-11-07 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB II |
| Status | Implemented |

> `Version` = this doc's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> `PRD Version` = the PRD Changelog timestamp this doc tracks. The two normally **differ** — never set them equal.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Migrated from `pm/openspec/specs/*.md` and reverse-designed against current `src/` on the `ncs264` branch (NCS v2.6.4, dual-board, `network` module replacing `wifi` module, `heap_monitor` added) |

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
| [2-dts-partition.md](2-dts-partition.md) | Flash partition layout per board (legacy Partition Manager, NCS v2.6.4) | FR-006, NFR flash/OTA |
| [3-memopt.md](3-memopt.md) | Memory optimization — stack watermarks, heap budget, headroom | NFR memory |
| [button-module.md](button-module.md) | Button SMF state machine, press actions | FR-003, FR-004 |
| [network-module.md](network-module.md) | Wi-Fi STA connectivity, L2/L3 event management, SoftAP groundwork | FR-001, FR-006 |
| [app-wifi-prov-ble-module.md](app-wifi-prov-ble-module.md) | Wi-Fi credential provisioning via BLE | FR-001 |
| [app-memfault-module.md](app-memfault-module.md) | Memfault core (upload/DNS-wait), metrics, OTA triggers, nRF70 stats CDR | FR-002, FR-003, FR-004, FR-101 |
| [app-https-client-module.md](app-https-client-module.md) | Always-on periodic HTTPS client | FR-005 |
| [app-mqtt-client-module.md](app-mqtt-client-module.md) | Always-on TLS MQTT echo client | FR-005 |
| [heap-monitor-module.md](heap-monitor-module.md) | System/mbedTLS heap tracking, Memfault heap metrics | FR-007 |

---

## 3. Architecture Summary

**Pattern**: SMF + Zbus modular

**Key design decisions:**

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture pattern | SMF + Zbus | Decoupled modules; Zbus (`BUTTON_CHAN`, `WIFI_CHAN`, `NETWORK_CHAN`) is the only inter-module channel |
| Wi-Fi/network module split | `network/net_event_mgmt.c` (L2/L3 event handling) + `network/wifi_utils.c` (helpers: mode/channel/credentials) | Legacy `wifi/wifi.c` was split for clarity; SoftAP event handling and STA connect logic now live in one file |
| Configuration | `prj.conf` (system-level + module enable flags) + per-module `Kconfig.<name>` and `Kconfig.defaults` | Each module owns its tuning defaults; `prj.conf` only overrides project-specific values and Kconfig `choice` symbols |
| Credentials | Wi-Fi: `wifi_credentials` NVS backend. Memfault: git-ignored `overlay-app-memfault-project-info.conf` | Never in source control |
| Partitioning | Legacy Partition Manager (`pm_static_<board>.yml`) | NCS v2.6.4 predates the DTS fixed-partitions migration (NCS v3.3+) |
| Dual-board support | Shared `src/modules/`; board differences isolated to `boards/*.conf`, `boards/*.overlay`, `pm_static_*.yml`, `sysbuild/mcuboot/boards/*` | One application core across nRF7002DK and nRF54LM20DK+nRF7002EB II |

---

## 4. PRD-to-Spec Mapping

| PRD requirement | Spec file | Status |
|----------------|-----------|--------|
| FR-001 Wi-Fi STA connect + provisioning | network-module.md, app-wifi-prov-ble-module.md | Specified |
| FR-002 Memfault upload on connect | app-memfault-module.md | Specified |
| FR-003 Button 1 heartbeat / stack-overflow demo | button-module.md, app-memfault-module.md | Specified |
| FR-004 Button 2 OTA / division-by-zero demo | button-module.md, app-memfault-module.md | Specified |
| FR-005 Always-on HTTPS/MQTT clients | app-https-client-module.md, app-mqtt-client-module.md | Specified |
| FR-006 Dual-board support | 1-architecture.md, 2-dts-partition.md | Specified |
| FR-007 Heap monitor → Memfault metrics | heap-monitor-module.md | Specified |
| FR-101 nRF70 stats CDR | app-memfault-module.md | Specified |
| FR-201 SoftAP/P2P (P2, not implemented) | — | Out of scope (see PRD §8) |

---

## 5. Module Dependency Map

```
network        ──Zbus(WIFI_CHAN)──▶  app_memfault (core)
network        ──Zbus(WIFI_CHAN)──▶  app_memfault (ota_triggers)
network        ──Zbus(WIFI_CHAN)──▶  app_wifi_prov_over_ble
network        ──Zbus(WIFI_CHAN)──▶  app_https_client
network        ──Zbus(WIFI_CHAN)──▶  app_mqtt_client
network        ──Zbus(NETWORK_CHAN)─▶ (reserved, no subscribers yet)
button         ──Zbus(BUTTON_CHAN)──▶ app_memfault (core)
button         ──Zbus(BUTTON_CHAN)──▶ app_memfault (ota_triggers)
heap_monitor   ──direct call────────▶ Memfault metrics API (no Zbus)
```

> For the full Zbus channel table, see [1-architecture.md](1-architecture.md).

---

## 6. Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| 1 | `NETWORK_CHAN` has no subscribers yet — reserved for future IP-layer-readiness consumers | — | — |
| 2 | SoftAP/P2P Kconfig and event-handler scaffolding present in `network` module but unused; decide whether to finish or remove | — | — |
| 3 | 24-hour soak test and full ZView-based memory measurement pass not yet run on either board (see [3-memopt.md](3-memopt.md)) | — | Next hardware validation pass |

*(Changelog is maintained at the top of this document.)*
