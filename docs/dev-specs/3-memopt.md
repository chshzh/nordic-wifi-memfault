# Memory Optimization Report — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-08-19-16-00 |
| NCS Version | v2.6.4 |
| Target Board(s) | nRF7002DK |
| Method | Not yet re-measured on this branch — see Open Issues. Figures below are carried over from the pre-migration `pm/QA.md` review (NCS v3.2.4, nRF7002DK only) for continuity. |
| Status | Draft |

> `Version` = this doc's own latest edit time (`date +%Y-%m-%d-%H-%M`); bump it on **every** edit.
> No `PRD Version` field — this doc tracks code, not product requirements.

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-13-11-08 | Migrated from `pm/QA.md` (2026-03-03 review, NCS v3.2.4, nRF7002DK). No live measurement pass yet performed on the current NCS v2.6.4 code — flagged as an Open Issue for `chsh-sk-ncs-3.3-memopt`. |
| 2026-07-24-11-30 | `tcp_work` stack overflow found via a symbolicated Memfault coredump trace (issue "Assert at k_spin_unlock [Stack Overflow in tcp_work, ...]"): `CONFIG_NET_TCP_WORKQ_STACK_SIZE` was left at the Zephyr default (1024 B, never overridden in `prj.conf`); the faulting PSP was `work_q_stack+920` — within ~104 B of the top — when HTTPS/TLS and MQTT both resumed traffic concurrently right after a Wi-Fi reconnect. The overflow corrupted the adjacent `struct k_work_q tcp_work_q`'s work-item handler pointer, producing `ASSERTION FAIL [handler != ((void *)0)] @ zephyr/kernel/work.c:669` → USAGE FAULT. Fixed by setting `CONFIG_NET_TCP_WORKQ_STACK_SIZE=2048`. |
| 2026-07-28-08-15 | **Same Memfault issue recurred** (still open, same issue id, trace_count 9) — 7 more `tcp_work` crashes in the field on `v2.6.4.1` over 2026-07-27/28, faulting PSP now at `work_q_stack+1944` (within ~104 B of the *2048* top — actual stack usage roughly doubled since the first fix, same razor-thin-margin pattern recurring one level up). The 1024→2048 bump was not enough headroom. Doubled again to `CONFIG_NET_TCP_WORKQ_STACK_SIZE=4096`; rebuilt clean (FLASH 66.92%, RAM 72.74%, no measurable RAM regression from +2048 B). Root cause of *why* tcp_work's usage keeps growing (NTP module added 2026-07-24? more concurrent TCP retransmission paths under heavier load?) not fully isolated — flagged as an Open Issue below; a real ZView watermark pass is needed instead of inferring the minimum from crash PSP offsets a second time. |
| 2026-08-19-16-00 | **Removed nRF54LM20DK project-wide** — no board definition for this SoC exists in NCS v2.6.4 (see [1-architecture.md](1-architecture.md) Changelog). Dropped the nRF54LM20DK watermark/board columns from every table below; this report now tracks nRF7002DK only. Note: `CONFIG_NET_TCP_WORKQ_STACK_SIZE` and several other stack sizes in `prj.conf` were originally tuned from nRF54LM20DK watermarks (higher due to CRACEN crypto stack depth), so nRF7002DK's own headroom may be more conservative than strictly necessary — not yet re-measured against nRF7002DK-only watermarks specifically. |

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

| Thread / WQ | Kconfig | nRF7002DK watermark (B) | Worst-case | Rule | New size | Old size | Δ (B) |
|-------------|---------|------------------------|------------|------|----------|----------|-------|
| `sysworkq` | `SYSTEM_WORKQUEUE_STACK_SIZE` | — | — | ÷0.9 | | | |
| `main` | `MAIN_STACK_SIZE` | — | — | ÷0.9 | | | |
| `memfault_upload_tid` | `MEMFAULT_UPLOAD_THREAD_STACK_SIZE` | — | — | ÷0.9 | | | |
| `mflt_ota_triggers_tid` | `MEMFAULT_OTA_THREAD_STACK_SIZE` | — | — | ÷0.9 | | | |
| `adv_daemon` (BLE prov work queue) | `ADV_DAEMON_STACK_SIZE` (hardcoded 8192 in `wifi_prov_over_ble.c`) | — | — | ÷0.9 | | | |
| `wpa_supplicant` | `WPA_SUPP_THREAD_STACK_SIZE` | — | — | ÷0.9 | | | |
| `wpa_supplicant_wq` | `WPA_SUPP_WQ_STACK_SIZE` | — | — | ÷0.9 | | | |
| `conn_mgr_monitor` | `NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | — | — | ÷0.9 | | | |
| `app_https_client` | `APP_HTTPS_CLIENT_STACK_SIZE` | — | — | ÷0.9 | | | |
| `app_mqtt_client` | `APP_MQTT_CLIENT_STACK_SIZE` | — | — | ÷0.9 | | | |
| `rx_q` | `NET_RX_STACK_SIZE` | — | — | kept | 2048 | 2048 | 0 |
| `tx_q` | `NET_TX_STACK_SIZE` | — | — | kept | 2048 | 2048 | 0 |
| `tcp_work` | `NET_TCP_WORKQ_STACK_SIZE` | 920 then 1944 (coredump PSP offsets, not a full watermark pass) | crashed twice (USAGE FAULT, stack overflow), same issue recurring at 2x depth | doubled again after 2nd crash | 4096 | 2048 | +2048 |

---

## Heap Analysis

> Not yet measured on this branch. `heap_monitor` module (new since migration) already
> logs system-heap and mbedTLS-heap usage at runtime (`CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC`)
> — use its UART output as a starting point for the next ZView pass.

| Heap | ZView pool name | nRF7002DK watermark (B) | Worst-case | New size | Old size | Δ (B) |
|------|-----------------|------------------------|------------|----------|----------|-------|
| System heap | `_system_heap` (`CONFIG_HEAP_MEM_POOL_SIZE`) | — | — | | | |
| mbedTLS heap | `CONFIG_MBEDTLS_ENABLE_HEAP` pool | — | — | | | |

---

## ISR Stack

| Board | ISR usage (B) | Allocated (B) | Utilization |
|-------|--------------|---------------|-------------|
| nRF7002DK | — | 2048 | — |

> `CONFIG_ISR_STACK_SIZE` defaults to 2048 B. Increase only if usage exceeds 80 %.

---

## Flash & RAM Budget

> Legacy figures from `pm/QA.md` (NCS v3.2.4, nRF7002DK): Flash at 94.47% utilization —
> flagged there as "tight and warrants monitoring". Needs re-measurement on NCS v2.6.4
> after a clean `west build`.

| Board | Flash used | Flash avail | Flash headroom | RAM used | RAM avail | RAM headroom |
|-------|-----------|------------|----------------|---------|----------|--------------|
| nRF7002DK | — KB | — KB | — % (legacy: 94.47% used → ~5.5% headroom) | — KB | — KB | — % |

---

## Summary of Changes Applied

| Kconfig | Old | New | Δ (B) | Reason |
|---------|-----|-----|-------|--------|
| `CONFIG_NET_TCP_WORKQ_STACK_SIZE` | 1024 | 2048 | +1024 | `tcp_work` stack overflow diagnosed via symbolicated Memfault coredump trace (see Changelog 2026-07-24-11-30) |
| `CONFIG_NET_TCP_WORKQ_STACK_SIZE` | 2048 | 4096 | +2048 | Same issue recurred in the field 7 more times on `v2.6.4.1` (see Changelog 2026-07-28-08-15); the first bump wasn't enough headroom |

---

## Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| 1 | Run a full ZView-based thread/heap watermark measurement pass on nRF7002DK (NCS v2.6.4) — the legacy figures above predate the `network` module split and `heap_monitor` addition | — | Next `chsh-sk-ncs-3.3-memopt` pass |
| 2 | Confirm current Flash headroom given legacy report flagged 94.47% utilization as tight — may now be worse with `heap_monitor` added | — | Next memopt pass |
| 3 | Re-measure after first OTA download cycle to capture `DOWNLOADER_STACK_SIZE` peak | — | Next OTA test |
