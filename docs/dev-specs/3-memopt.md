# Memory Optimization Report — nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-23-09-53 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK (nRF5340) |
| Method | Thread Analyzer (`CONFIG_THREAD_ANALYZER_AUTO`) + heap_monitor, steady-state after Wi-Fi STA connect, Memfault heartbeat, MQTT publish, and HTTPS GET cycle |
| Status | Applied |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-23-09-53 | net_mgmt stack manually increased 3510→4480 |
| 2026-06-22-12-40 | Third measurement pass: WiFi + WPA supplicant global heaps split into 3 dedicated K_HEAPs; system heap drops from 72 410 B to 1 440 B; all thread stacks re-measured post BLE provisioning addition; MEMFAULT_LOGGING_RAM_SIZE 4096→8192 |
| 2026-05-21-17-32 | Second measurement pass (nRF54LM20DK log: WiFi timeout + reconnect + Memfault upload cycle); sysworkq overflow root-caused and fixed; WiFi thread oversizing corrected; ~52 KB RAM saved |
| 2026-05-14-00-00 | Initial measurement and stack/heap sizing applied to prj.conf |

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

---

## Measurement Update — 2026-05-21

**Source:** `ncs_nrf54_latest.log` (nRF54LM20DK, LOG_MODE_IMMEDIATE enabled, T=33–125 s covering WiFi timeout, reconnect, and first Memfault upload). nRF7002DK data was early-boot only (T=1.6 s) and not used for sizing.

**Root cause resolved:** `sysworkq` was overflowing at 2728 B during the WiFi reconnect path (`wpa_cli_cmd`[1024] + `_wpa_ctrl_command`[512] + `zvfs_poll_internal`[400] = ~4488 B actual peak). This caused silent `RESETREAS=0x40` reboots. The WiFi thread stacks (`hostap_*`, `nrf70_*`, `conn_mgr_monitor`) had been inflated defensively during overflow investigation; they are now correctly sized after the sysworkq fix.

### Thread Stack Measurements (2026-05-21)

| Thread name | Kconfig | nRF54LM20DK new | Old max | New max | New size | Old size | Δ | Note |
|-------------|---------|----------------|---------|---------|----------|----------|---|------|
| `sysworkq` | `SYSTEM_WORKQUEUE_STACK_SIZE` | 4488 / 6144 (73 %) | 2456 | **4488** | **4988** | 2728 | +2260 | WiFi reconnect path overflow (root cause fix) |
| `main` | `MAIN_STACK_SIZE` | 1496 / 2048 (73 %) | 1448 | **1496** | **1662** | 2048 | −386 | |
| `hostap_handler` | `WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE` | 2832 / 20352 (13 %) | 5392 | **5392** ¹ | **5991** | 20240 | −14249 | ¹ 5392 from 2026-05-14 steady-state; new log is reconnect-only |
| `hostap_iface_wq` | `WIFI_NM_WPA_SUPPLICANT_WQ_STACK_SIZE` | 3896 / 20480 (19 %) | 3856 | **3896** | **4328** | 20480 | −16152 | |
| `conn_mgr_monitor` | `NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | 4432 / 16384 (27 %) | 4424 | **4432** | **4924** | 16384 | −11460 | |
| `nrf70_bh_wq` | `NRF70_BH_WQ_STACK_SIZE` | 1088 / 4096 (26 %) | — | **1088** | **1208** | 4096 | −2888 | First measurement; WiFi reconnect |
| `nrf70_intr_wq` | `NRF70_IRQ_WQ_STACK_SIZE` | 792 / 4096 (19 %) | — | **792** | **880** | 4096 | −3216 | First measurement; WiFi reconnect |
| `net_mgmt` | `NET_MGMT_EVENT_STACK_SIZE` | 1336 / 8192 (16 %) | 2940 | 2940 ² | **3266** | 8192 | −4926 | ² New log early-boot; retained prior peak 2940 |
| `thread_analyzer` | `THREAD_ANALYZER_AUTO_STACK_SIZE` | 1216 / 4096 (29 %) ³ | 568 | **568** | **631** | 2048 | −1417 | ³ Measured with IMMEDIATE mode; DEFERRED peak is 568 |

Heap unchanged: no heap stats available from current logs; 2026-05-14 measurements remain the reference.

### Summary of Changes Applied — 2026-05-21

| Kconfig | Old | New | Δ B | Reason |
|---------|-----|-----|-----|--------|
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | 2728 | **4988** | +2260 | Overflow in WiFi reconnect path (wpa_cli_cmd stack depth) |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE` | 20240 | **5991** | −14249 | Root cause fixed; resize to measured steady-state max |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_WQ_STACK_SIZE` | 20480 | **4328** | −16152 | Root cause fixed; 3896 peak confirmed |
| `CONFIG_NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | 16384 | **4924** | −11460 | Root cause fixed; 4432 peak confirmed |
| `CONFIG_NRF70_BH_WQ_STACK_SIZE` | 4096 | **1208** | −2888 | Root cause fixed; first measurement 1088 |
| `CONFIG_NRF70_IRQ_WQ_STACK_SIZE` | 4096 | **880** | −3216 | Root cause fixed; first measurement 792 |
| `CONFIG_NET_MGMT_EVENT_STACK_SIZE` | 8192 | **3266** | −4926 | Correctly sized per existing measurement (2940) |
| `CONFIG_MAIN_STACK_SIZE` | 2048 | **1662** | −386 | New peak 1496 > prior max 1448 |
| `CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE` | 2048 | **631** | −1417 | DEFERRED mode; was left at 2048 after debug session |

**Net stack RAM change (2026-05-21):** −52 434 B (∲52 KB saved)


| Board | ISR0 usage | Allocated | Utilization |
|-------|-----------|-----------|-------------|
| nRF54LM20DK | 560 B | 2048 B | 27 % |
| nRF7002DK | 944 B | 2048 B | 46 % |

ISR stack (`CONFIG_ISR_STACK_SIZE`) is at default (2048 B) and comfortably within bounds. No change needed.

---

---

## Measurement Update — 2026-06-22

**Source:** ZView watermark measurements after BLE provisioning (ZEGO_WIFI_BLE_PROV) integration; full STA connect + Memfault upload + MQTT publish + HTTPS GET cycle. Sizing rules: 20 % headroom (÷ 0.8) for watermark < 5120 B, 10 % headroom (÷ 0.9) for watermark ≥ 5120 B.

**Key structural change — heap split:** `NRF_WIFI_GLOBAL_HEAP=n` and `WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=n` are now set. The Wi-Fi driver and WPA supplicant are each allocated from their own dedicated `K_HEAP` (visible as `wifi_drv_ctrl_mem_pool`, `wifi_drv_data_mem_pool`, and `wifi_nm_wpa_supplicant_mem_pool` in ZView). All `HEAP_MEM_POOL_ADD_SIZE_*` contributions from nRF70, hostap, board, and POSIX are removed, so `_system_heap` now reflects **app-only** usage and drops dramatically.

### Thread Stack Watermarks (2026-06-22)

| Thread / WQ | Kconfig | Watermark (B) | Rule | New size | Old size | Δ (B) |
|-------------|---------|--------------|------|----------|----------|-------|
| `sysworkq` | `SYSTEM_WORKQUEUE_STACK_SIZE` | 4528 | ÷0.8 | **5660** | 4988 | +672 |
| `main` | `MAIN_STACK_SIZE` | 1496 | ÷0.9 | **1662** | 1662 | 0 |
| `idle` | `IDLE_STACK_SIZE` | 248 | ÷0.8→310 | **320** (kept) | — | — |
| `memfault_upload_tid` | `MEMFAULT_UPLOAD_THREAD_STACK_SIZE` | 4928 | ÷0.8 | **6160** | 5413 ¹ | +747 |
| `mflt_upload` | `MEMFAULT_PERIODIC_UPLOAD_DEDICATED_WORKQUEUE_STACK_SIZE` | 4768 | ÷0.8 | **5960** | — ² | new |
| `mflt_ota_triggers_tid` | `MEMFAULT_OTA_THREAD_STACK_SIZE` | 5064 | ÷0.8 | **6330** | 5600 | +730 |
| downloader | `DOWNLOADER_STACK_SIZE` | 4824 | ÷0.8 | **6030** | 5360 | +670 |
| `app_https_client_tid` | `APP_HTTPS_CLIENT_STACK_SIZE` | 5024 | ÷0.8 | **6280** | 5582 | +698 |
| `app_mqtt_client_tid` | `APP_MQTT_CLIENT_STACK_SIZE` | 5272 | ÷0.9 | **5857** | 5848 | +9 |
| `mqtt_helper_thread` | `MQTT_HELPER_STACK_SIZE` | 4040 | ÷0.8 | **5050** | 4488 | +562 |
| `BT LW WQ` | `BT_LONG_WQ_STACK_SIZE` | 2008 | ÷0.8 | **2510** | 1591 | +919 |
| `BT RX WQ` | `BT_RX_STACK_SIZE` | 20080 | ÷0.9 | **22311** | 21123 | +1188 |
| `bt_tx_processor` | `BT_TX_PROCESSOR_STACK_SIZE` | 408 | ÷0.8 | **510** | — | new |
| `ble_adv_daemon_wq` | `ZEGO_WIFI_BLE_PROV_ADV_DAEMON_STACK_SIZE` | 2448 | ÷0.8 | **3060** | — | new |
| unnamed L2 WQ | `L2_WIFI_CONN_WQ_STACK_SIZE` | 248 | ÷0.9→276 | **276** | 276 | 0 |
| `hostap_handler` | `WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE` | 5384 | ÷0.9 | **5982** | 5991 | −9 |
| `hostap_iface_wq` | `WIFI_NM_WPA_SUPPLICANT_WQ_STACK_SIZE` | 3848 | ÷0.8 | **4810** | 4328 | +482 |
| `nrf70_bh_wq` | `NRF70_BH_WQ_STACK_SIZE` | 1072 | ÷0.8 | **1340** | 1208 | +132 |
| `nrf70_intr_wq` | `NRF70_IRQ_WQ_STACK_SIZE` | 672 | ÷0.8 | **840** | 880 | −40 |
| `rx_q` | `NET_RX_STACK_SIZE` | 1168 | kept | **2048** | 2048 | 0 |
| `tx_q` | `NET_TX_STACK_SIZE` | 800 | kept | **2048** | 2048 | 0 |
| `conn_mgr_monitor` | `NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | 4416 | ÷0.8 | **5520** | 4924 | +596 |
| `net_mgmt` | `NET_MGMT_EVENT_STACK_SIZE` | 2808 | ÷0.8 | **3510** | 3266 | +244 |
| `MPSL Work` | `MPSL_WORK_STACK_SIZE` | 696 | kept | **1024** | 1024 | 0 |
| `tcp_work` | `NET_TCP_WORKQ_STACK_SIZE` | 488 | kept | **1024** | 1024 | 0 |
| `logging` | `LOG_PROCESS_THREAD_STACK_SIZE` | 752 | kept | **2048** | 2048 | 0 |
| `net_socket_service` | (auto by Zephyr) | 784 | auto | — | — | — |
| `mbox_wq` | `IPC_SERVICE_BACKEND_RPMSG_WQ_STACK_SIZE` | 400 | kept | **1024** | 1024 | 0 |

¹ Previously tracked as `MEMFAULT_HTTP_DEDICATED_WORKQUEUE_STACK_SIZE` — Kconfig symbol renamed.  
² New symbol in this NCS version; previous workqueue was folded into this.

### Heap Watermarks (2026-06-22)

| Heap | ZView pool name | Watermark (B) | Rule | New size | Old size | Δ (B) |
|------|-----------------|--------------|------|----------|----------|-------|
| `_system_heap` (app-only) | `_system_heap` | 1152 | ÷0.8 | **1440** | 72410 | −70970 |
| mbedTLS heap | — | 69720 | ÷0.8 | **87150** | 86880 | +270 |
| Wi-Fi ctrl heap (new) | `wifi_drv_ctrl_mem_pool` | 10288 | ÷0.8 | **12860** | — | new |
| Wi-Fi data heap (new) | `wifi_drv_data_mem_pool` | 28520 | ÷0.8 | **35650** | — | new |
| WPA supplicant heap (new) | `wifi_nm_wpa_supplicant_mem_pool` | 24184 | ÷0.8 | **30230** | — | new |

> `HEAP_MEM_POOL_IGNORE_MIN=y` is still set — remove once app-only `_system_heap` watermark is confirmed stable at low usage over a full OTA + reconnect cycle.

### Summary of Changes Applied — 2026-06-22

| Kconfig | Old | New | Δ (B) | Reason |
|---------|-----|-----|-------|--------|
| `CONFIG_HEAP_MEM_POOL_SIZE` | 72410 | **1440** | −70970 | App-only heap after WiFi/WPA split; new watermark 1152 |
| `CONFIG_NRF_WIFI_GLOBAL_HEAP` | y | **n** | — | Moved to dedicated `NRF_WIFI_CTRL/DATA_HEAP` |
| `CONFIG_NRF_WIFI_CTRL_HEAP_SIZE` | — | **12860** | +12860 | New dedicated heap; watermark 10288 |
| `CONFIG_NRF_WIFI_DATA_HEAP_SIZE` | — | **35650** | +35650 | New dedicated heap; watermark 28520 |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP` | y | **n** | — | Moved to dedicated `WPA_SUPPLICANT_HEAP` |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_HEAP` | — | **30230** | +30230 | New dedicated heap; watermark 24184 |
| `CONFIG_MBEDTLS_HEAP_SIZE` | 86880 | **87150** | +270 | Re-measured; watermark 69720 |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | 4988 | **5660** | +672 | New watermark 4528 (BLE prov path) |
| `CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE` | 5413 | **6160** | +747 | Re-measured watermark 4928 |
| `CONFIG_MEMFAULT_PERIODIC_UPLOAD_DEDICATED_WORKQUEUE_STACK_SIZE` | — | **5960** | +5960 | New symbol; watermark 4768 |
| `CONFIG_MEMFAULT_OTA_THREAD_STACK_SIZE` | 5600 | **6330** | +730 | Re-measured watermark 5064 |
| `CONFIG_DOWNLOADER_STACK_SIZE` | 5360 | **6030** | +670 | Re-measured watermark 4824 |
| `CONFIG_APP_HTTPS_CLIENT_STACK_SIZE` | 5582 | **6280** | +698 | Re-measured watermark 5024 |
| `CONFIG_APP_MQTT_CLIENT_STACK_SIZE` | 5848 | **5857** | +9 | Re-measured watermark 5272 |
| `CONFIG_MQTT_HELPER_STACK_SIZE` | 4488 | **5050** | +562 | Re-measured watermark 4040 |
| `CONFIG_BT_LONG_WQ_STACK_SIZE` | 1591 | **2510** | +919 | Re-measured watermark 2008 (BLE prov active) |
| `CONFIG_BT_RX_STACK_SIZE` | 21123 | **22311** | +1188 | Re-measured watermark 20080 |
| `CONFIG_BT_TX_PROCESSOR_STACK_SIZE` | — | **510** | +510 | New thread; watermark 408 |
| `CONFIG_ZEGO_WIFI_BLE_PROV_ADV_DAEMON_STACK_SIZE` | — | **3060** | +3060 | New thread; watermark 2448 |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE` | 5991 | **5982** | −9 | Re-measured watermark 5384 |
| `CONFIG_WIFI_NM_WPA_SUPPLICANT_WQ_STACK_SIZE` | 4328 | **4810** | +482 | Re-measured watermark 3848 |
| `CONFIG_NRF70_BH_WQ_STACK_SIZE` | 1208 | **1340** | +132 | Re-measured watermark 1072 |
| `CONFIG_NRF70_IRQ_WQ_STACK_SIZE` | 880 | **840** | −40 | Re-measured watermark 672 |
| `CONFIG_NET_CONNECTION_MANAGER_MONITOR_STACK_SIZE` | 4924 | **5520** | +596 | Re-measured watermark 4416 |
| `CONFIG_NET_MGMT_EVENT_STACK_SIZE` | 3266 | **3510** | +244 | Re-measured watermark 2808 |

**Net thread stack RAM change (2026-06-22):** +9 650 B (~9.4 KB)  
**Net heap RAM change (2026-06-22):** −70 970 (system) + 270 (mbedTLS) + 78 740 (new dedicated) = **+8 040 B** (~7.8 KB)  
**Total net RAM change:** +17 690 B (~17.3 KB) — offset by structural split that makes Wi-Fi heaps observable and right-sized in ZView.

---

## Configuration Update — 2026-06-23

**Change:** `net_mgmt` stack manually increased beyond the measurement-derived value. Prior watermark (2026-06-22): 2808 B; measurement-derived size: 3510 B (÷0.8). New value: 4480 B (~60 % headroom above watermark).

| Kconfig | Old | New | Δ (B) | Reason |
|---------|-----|-----|-------|--------|
| `CONFIG_NET_MGMT_EVENT_STACK_SIZE` | 3510 | **4480** | +970 | Manual increase for additional headroom |

**Net stack RAM change (2026-06-23):** +970 B

---

## Open Issues

| # | Description | Owner | Target |
|---|-------------|-------|--------|
| 1 | Rebuild and flash both boards after these changes to confirm no regression | Team | Next build cycle |
| 2 | Re-run thread analyzer after a full OTA download cycle to capture `DOWNLOADER_STACK_SIZE` high-water mark | Team | Next OTA test |
| 3 | Monitor system heap — with the new 72 410 B ceiling the peak is 80 %; raise to 80 000 B if future features push it higher | Team | Ongoing |
| 4 | `BT RX WQ` is grossly oversized (248 B used of 21 248 B, 1 %); `CONFIG_BT_RX_STACK_SIZE` cannot easily be reduced due to Zephyr minimum tied to max HCI event size — leave as-is | — | — |
