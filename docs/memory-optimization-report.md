# Memory Optimization Report — nordic-wifi-memfault

**Date:** 2026-05-14  
**NCS version:** v3.3.0  
**Boards measured:** nRF7002DK (nRF5340) · nRF54LM20DK + nRF7002EB2  
**Method:** Thread Analyzer (`CONFIG_THREAD_ANALYZER_AUTO`) + heap_monitor, both at steady-state after Wi-Fi STA connect, Memfault heartbeat, MQTT publish, and HTTPS GET cycle.

---

## Sizing Rules

| Resource | Formula | Headroom |
|----------|---------|---------|
| Thread stacks | `floor(max_usage / 0.9)` | 10 % |
| System heap | `floor(peak / 0.8)` | 20 % |
| mbedTLS heap | `floor(peak / 0.8)` | 20 % |

Worst-case value across **both boards** is used. `NET_RX_STACK_SIZE` and `NET_TX_STACK_SIZE` are kept at the Zephyr default (2048 B) regardless of measurement to absorb network burst spikes.

---

## Thread Stack Analysis

| Thread name | Kconfig | nRF7002DK usage | nRF54LM20DK usage | Max | New size | Old size | Δ | Risk |
|-------------|---------|----------------|------------------|-----|----------|----------|---|------|
| `mqtt_helper_thread` | `MQTT_HELPER_STACK_SIZE` | 2728 / 4224 (64 %) | 4040 / 4224 **(95 %)** | 4040 | **4488** | 4160 | +328 | ⚠ CRITICAL |
| `app_https_client_tid` | `APP_HTTPS_CLIENT_STACK_SIZE` | 5024 / 5504 **(91 %)** | 4752 / 5504 (86 %) | 5024 | **5582** | 5471 | +111 | HIGH |
| `app_mqtt_client_tid` | `APP_MQTT_CLIENT_STACK_SIZE` | 5264 / 5760 **(91 %)** | 4992 / 5760 (86 %) | 5264 | **5848** | 5751 | +97 | HIGH |
| `mflt_ota_triggers_tid` | `MEMFAULT_OTA_THREAD_STACK_SIZE` | 5040 / 5632 (89 %) | 4824 / 5632 (85 %) | 5040 | **5600** | 5569 | +31 | MEDIUM |
| `thread_analyzer` | `THREAD_ANALYZER_AUTO_STACK_SIZE` | 536 / 640 (83 %) | 568 / 640 (88 %) | 568 | **631** | 605 | +26 | LOW |
| `hostap_handler` | `WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE` | 5312 / 6144 (86 %) | 5392 / 6144 (87 %) | 5392 | **5991** | 6098 | −107 | LOW |
| `hostap_iface_wq` | `WIFI_NM_WPA_SUPPLICANT_WQ_STACK_SIZE` | 3848 / 4352 (88 %) | 3856 / 4352 (88 %) | 3856 | **4284** | 4275 | +9 | LOW |
| `conn_mgr_monitor` | `NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | 4416 / 4992 (88 %) | 4424 / 4992 (88 %) | 4424 | **4915** | 4973 | −58 | LOW |
| `memfault_upload_tid` | `MEMFAULT_HTTP_DEDICATED_WORKQUEUE_STACK_SIZE` | 4840 / 5504 (87 %) | 4672 / 5504 (84 %) | 4840 | **5377** | 5404 | −27 | LOW |
| `mflt_upload` | `MEMFAULT_UPLOAD_THREAD_STACK_SIZE` | 4872 / 5504 (88 %) | 4536 / 5504 (82 %) | 4872 | 5413 | 5413 | 0 | — |
| `main` | `MAIN_STACK_SIZE` | 1320 / 1664 (79 %) | 1448 / 1664 (87 %) | 1448 | 1608 | 1608 | 0 | — |
| `sysworkq` | `SYSTEM_WORKQUEUE_STACK_SIZE` | 2448 / 2816 (86 %) | 2456 / 2816 (87 %) | 2456 | 2728 | 2728 | 0 | — |
| `BT LW WQ` | `BT_LONG_WQ_STACK_SIZE` | 1120 / 1664 (67 %) | 1432 / 1664 (86 %) | 1432 | 1591 | 1591 | 0 | — |
| `BT RX WQ` | `BT_RX_STACK_SIZE` | 240 / 21248 (1 %) | 248 / 21248 (1 %) | 248 | 21123 | 21123 | 0 | — |
| unnamed L2 WQ | `L2_WIFI_CONN_WQ_STACK_SIZE` | 240 / 384 (62 %) | 248 / 384 (64 %) | 248 | 276 | 276 | 0 | — |
| `net_mgmt` | `NET_MGMT_EVENT_STACK_SIZE` | 2576 / 2944 (87 %) | 2584 / 2944 (87 %) | 2584 | 2871 | 2871 | 0 | — |
| `rx_q[0]` | `NET_RX_STACK_SIZE` | 776 / 2048 (37 %) | 1168 / 2048 (57 %) | 1168 | kept 2048 | 2048 | 0 | — |
| `tx_q[0]` | `NET_TX_STACK_SIZE` | 672 / 2048 (32 %) | 792 / 2048 (38 %) | 792 | kept 2048 | 2048 | 0 | — |
| downloader | `DOWNLOADER_STACK_SIZE` | not active | not active | — | kept 5360 | 5360 | 0 | — |

> **Note on `DOWNLOADER_STACK_SIZE`:** The Memfault OTA download thread was not active during capture (no OTA in progress). Previous measurement of 4824 B is retained.

---

## Heap Analysis

| Heap | nRF7002DK | nRF54LM20DK | Peak (worst) | New size | Old size | Δ |
|------|-----------|-------------|-------------|----------|----------|---|
| System heap (`HEAP_MEM_POOL_SIZE`) | used 51824 / peak **57928** / 64696 **(89 %)** | used 51168 / peak 57272 / 64696 (88 %) | 57928 | **72410** | 64780 | **+7630** |
| mbedTLS heap (`MBEDTLS_HEAP_SIZE`) | used 21648 / peak 68412 / 89960 (76 %) | used 21696 / peak **69504** / 89960 (77 %) | 69504 | **86880** | 89960 | −3080 |

The system heap was peaking at **89 %** of its configured ceiling — within a single allocation burst of exhaustion. Raising the ceiling from 64 780 B to 72 410 B brings headroom back to 20 % above the measured peak.

---

## Summary of Changes Applied to `prj.conf`

| Kconfig | Old | New | Δ B | Reason |
|---------|-----|-----|-----|--------|
| `CONFIG_MQTT_HELPER_STACK_SIZE` | 4160 | **4488** | +328 | 95 % usage on nRF54LM20DK — stack overflow risk |
| `CONFIG_HEAP_MEM_POOL_SIZE` | 64780 | **72410** | +7630 | System heap peak at 89 % of ceiling |
| `CONFIG_APP_HTTPS_CLIENT_STACK_SIZE` | 5471 | **5582** | +111 | 91 % usage on nRF7002DK |
| `CONFIG_APP_MQTT_CLIENT_STACK_SIZE` | 5751 | **5848** | +97 | 91 % usage on nRF7002DK |
| `CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE` | 5569 | **5600** | +31 | 89 % usage on nRF7002DK |
| `CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE` | 605 | **631** | +26 | 88 % usage on nRF54LM20DK |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_WQ_STACK_SIZE` | 4275 | **4284** | +9 | Measurement update |
| `CONFIG_NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | 4973 | **4915** | −58 | Measurement-accurate recalc |
| `CONFIG_MEMFAULT_HTTP_DEDICATED_WORKQUEUE_STACK_SIZE` | 5404 | **5377** | −27 | Measurement-accurate recalc |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE` | 6098 | **5991** | −107 | Measured max dropped to 5392 |
| `CONFIG_MBEDTLS_HEAP_SIZE` | 89960 | **86880** | −3080 | Peak dropped to 69504 |
| `CONFIG_NET_RX_STACK_SIZE` | 2048 | 2048 | 0 | Measured 1168; kept for burst headroom |
| `CONFIG_NET_TX_STACK_SIZE` | 2048 | 2048 | 0 | Measured 792; kept for burst headroom |

**Net stack RAM change:** +428 B  
**Net heap RAM change:** +4550 B  
**Total net RAM change:** +4978 B (~5 KB)

---

## ISR Stack

| Board | ISR0 usage | Allocated | Utilization |
|-------|-----------|-----------|-------------|
| nRF7002DK | 944 B | 2048 B | 46 % |
| nRF54LM20DK | 560 B | 2048 B | 27 % |

ISR stack (`CONFIG_ISR_STACK_SIZE`) is at default (2048 B) and comfortably within bounds. No change needed.

---

## Action Items

- [ ] **Rebuild and flash both boards** after this change to confirm no regression.
- [ ] **Re-run thread analyzer** after a full OTA download cycle to capture `DOWNLOADER_STACK_SIZE` high-water mark.
- [ ] **Monitor system heap** — with the new 72 410 B ceiling the peak is 80 %. If future features push it higher, consider raising to 80 000 B.
- [ ] **BT RX WQ** is grossly oversized (248 B used of 21 248 B, 1 %). `CONFIG_BT_RX_STACK_SIZE` cannot easily be reduced because Zephyr enforces a minimum tied to the maximum HCI event size. Leave as-is.
