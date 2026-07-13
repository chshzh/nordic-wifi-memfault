# Memory Optimization Report — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-07-13-11-08 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB II |
| Method | Not yet re-measured on this branch — see Open Issues. Figures below are carried over from the pre-migration `pm/QA.md` review (NCS v3.2.4, nRF7002DK only) for continuity. |
| Status | Draft |

> `Version` = this doc's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> No `PRD Version` field — this doc tracks code, not product requirements.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Migrated from `pm/QA.md` (2026-03-03 review, NCS v3.2.4, nRF7002DK). No live measurement pass yet performed on the current NCS v2.6.4 / dual-board code — flagged as an Open Issue for `chsh-sk-ncs-3.3-memopt`. |

---

## Sizing Rules

| Resource | Formula | Headroom |
|----------|---------|---------|
| Thread stacks | `floor(watermark / 0.9)` | 10 % |
| Heaps | `floor(peak / 0.8)` | 20 % |

`NET_RX_STACK_SIZE` and `NET_TX_STACK_SIZE` are kept at the Zephyr default (2048 B) regardless
of measurement to absorb network burst spikes.

---

## Headroom Targets

| Resource | Minimum headroom |
|----------|-----------------|
| Internal Flash | > 10 % of SoC flash |
| RAM (total) | > 5 % of SoC RAM |
| `settings_storage` (8 KB NVS) | > 2 KB free |

---

## Thread Stack Analysis

> Not yet measured on NCS v2.6.4 / current module set (network, button, heap_monitor,
> app_memfault core+ota, app_https_client, app_mqtt_client, wifi_prov_over_ble adv_daemon).
> Fill after a ZView watermark pass per `chsh-sk-ncs-3.3-memopt`.

| Thread / WQ | Kconfig | nRF7002DK watermark (B) | nRF54LM20DK watermark (B) | Worst-case | Rule | New size | Old size | Δ (B) |
|-------------|---------|------------------------|------------------------|------------|------|----------|----------|-------|
| `sysworkq` | `SYSTEM_WORKQUEUE_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `main` | `MAIN_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `memfault_upload_tid` | `MEMFAULT_UPLOAD_THREAD_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `mflt_ota_triggers_tid` | `MEMFAULT_OTA_THREAD_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `adv_daemon` (BLE prov work queue) | `ADV_DAEMON_STACK_SIZE` (hardcoded 8192 in `wifi_prov_over_ble.c`) | — | — | — | ÷0.9 | | | |
| `wpa_supplicant` | `WPA_SUPP_THREAD_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `wpa_supplicant_wq` | `WPA_SUPP_WQ_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `conn_mgr_monitor` | `NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `app_https_client` | `APP_HTTPS_CLIENT_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `app_mqtt_client` | `APP_MQTT_CLIENT_STACK_SIZE` | — | — | — | ÷0.9 | | | |
| `rx_q` | `NET_RX_STACK_SIZE` | — | — | — | kept | 2048 | 2048 | 0 |
| `tx_q` | `NET_TX_STACK_SIZE` | — | — | — | kept | 2048 | 2048 | 0 |

---

## Heap Analysis

> Not yet measured on this branch. `heap_monitor` module (new since migration) already
> logs system-heap and mbedTLS-heap usage at runtime (`CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC`)
> — use its UART output as a starting point for the next ZView pass.

| Heap | ZView pool name | nRF7002DK watermark (B) | nRF54LM20DK watermark (B) | Worst-case | New size | Old size | Δ (B) |
|------|-----------------|------------------------|------------------------|------------|----------|----------|-------|
| System heap | `_system_heap` (`CONFIG_HEAP_MEM_POOL_SIZE`) | — | — | — | | | |
| mbedTLS heap | `CONFIG_MBEDTLS_ENABLE_HEAP` pool | — | — | — | | | |

---

## ISR Stack

| Board | ISR usage (B) | Allocated (B) | Utilization |
|-------|--------------|---------------|-------------|
| nRF7002DK | — | 2048 | — |
| nRF54LM20DK | — | 2048 | — |

> `CONFIG_ISR_STACK_SIZE` defaults to 2048 B. Increase only if usage exceeds 80 %.

---

## Flash & RAM Budget

> Legacy figures from `pm/QA.md` (NCS v3.2.4, nRF7002DK): Flash at 94.47% utilization —
> flagged there as "tight and warrants monitoring". Needs re-measurement on NCS v2.6.4 for
> both boards after a clean `west build`.

| Board | Flash used | Flash avail | Flash headroom | RAM used | RAM avail | RAM headroom |
|-------|-----------|------------|----------------|---------|----------|--------------|
| nRF7002DK | — KB | — KB | — % (legacy: 94.47% used → ~5.5% headroom) | — KB | — KB | — % |
| nRF54LM20DK | — KB | — KB | — % | — KB | — KB | — % |

---

## Summary of Changes Applied

| Kconfig | Old | New | Δ (B) | Reason |
|---------|-----|-----|-------|--------|
| — | — | — | — | No changes applied yet in this migration pass |

---

## Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| 1 | Run a full ZView-based thread/heap watermark measurement pass on both boards (NCS v2.6.4) — the legacy figures above predate the `network` module split, `heap_monitor` addition, and dual-board port | — | Next `chsh-sk-ncs-3.3-memopt` pass |
| 2 | Confirm current Flash headroom given legacy report flagged 94.47% utilization as tight — may now be worse with `heap_monitor` added | — | Next memopt pass |
| 3 | Re-measure after first OTA download cycle to capture `DOWNLOADER_STACK_SIZE` peak | — | Next OTA test |
