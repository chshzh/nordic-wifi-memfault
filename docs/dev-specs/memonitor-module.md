# memonitor Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-06-19-12-44 |
| PRD Version | 2026-06-19-12-31 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Implemented |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-19-12-44 | PRD Version updated to 2026-06-19-12-31. |
| 2026-06-19-12-31 | New spec: replaces heap-monitor-module.md. Documents zego/memonitor brick — Zbus-based periodic heap and thread watermark sampling, ZView live monitoring, conditional Memfault metric feed via app_memfault_memonitor_metrics.c subscriber, and LOG_PERIODIC UART logging option. |

---

## Overview

The memonitor module is a reusable `zego/memonitor` brick registered as an
`EXTRA_ZEPHYR_MODULE`. It replaces the former `src/modules/heap_monitor/`
application module.

It periodically samples system heap and mbedTLS heap watermarks and all
Zephyr thread stack high-water marks, then publishes the results on
`MEMONITOR_CHAN` (Zbus). Subscribers consume this channel to feed Memfault
metrics and to provide ZView live monitoring.

---

## Location

- Source: `zego/bricks/memonitor/` (external brick, registered via `EXTRA_ZEPHYR_MODULES`)
- App subscriber: `src/modules/app_memfault/metrics/app_memfault_memonitor_metrics.c`
- Spec: `zego/bricks/memonitor/docs/memonitor-spec.md`

---

## Module Type

- External Zego brick (no app-owned source code in `src/modules/`)
- App-owned Memfault subscriber in `src/modules/app_memfault/metrics/`

---

## Zbus Integration

| Channel | Direction | Publisher | Subscribers |
|---------|-----------|-----------|-------------|
| `MEMONITOR_CHAN` | publish | `zego/memonitor` (on each sample interval) | `memonitor_metrics_listener` (app_memfault) |

The Memfault subscriber (`app_memfault_memonitor_metrics.c`) is registered
with `ZBUS_LISTENER_DEFINE` + `ZBUS_CHAN_ADD_OBS`. It uses file-scope static
buffers to avoid stack pressure.

---

## Kconfig Flags

### zego/memonitor brick (upstream)

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_ZEGO_MEMONITOR` | bool | n | Enable the brick |
| `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` | int | 5000 | Sample interval in ms |
| `CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR` | bool | y (auto) | Track heap HWM |
| `CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR` | bool | y (auto) | Track thread stack HWM |
| `CONFIG_ZEGO_MEMONITOR_ZVIEW` | bool | n | Enable ZView live monitoring over SWD/JTAG |
| `CONFIG_ZEGO_MEMONITOR_LOG_PERIODIC` | bool | n | Emit compact INF log line per heap/thread on each sample |
| `CONFIG_ZEGO_MEMONITOR_LOG_LEVEL` | int | 3 | Log level (0=OFF…4=DBG; 3=INF) |

### App overrides (prj.conf)

| Symbol | Value | Reason |
|--------|-------|--------|
| `CONFIG_ZEGO_MEMONITOR` | y | Enable |
| `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` | 5000 | Matches old heap_monitor cadence; drives Memfault metrics + ZView refresh |
| `CONFIG_ZEGO_MEMONITOR_ZVIEW` | y | Live heap/thread watermark monitoring over JLink |
| `CONFIG_STACK_SENTINEL` | n | Must be off for accurate ZView stack HWM readings |
| `CONFIG_ZEGO_MEMONITOR_LOG_LEVEL` | 3 | INF-level logging |
| `CONFIG_MEMFAULT_NCS_STACK_METRICS` | n | Disabled: memonitor subscriber provides thread metrics instead |

### Board overrides

| Symbol | Board | Value | Reason |
|--------|-------|-------|--------|
| `CONFIG_ZEGO_MEMONITOR_LOG_PERIODIC` | nRF54LM20DK | y | Sufficient flash headroom; disabled on nRF7002DK (flash-constrained) |

---

## Memfault Metric Feed

The subscriber maps named heaps and threads to compile-time Memfault metric keys:

| Heap name | Metric key |
|-----------|------------|
| `_system_heap` | `ncs_system_heap_largest_free_block_bytes` (unused) |
| `mbedtls_heap` | `ncs_mbedtls_heap_largest_free_block_bytes` (unused) |

| Thread name | Metric key | Guard |
|-------------|------------|-------|
| `hostap_iface_wq` | `ncs_wifi_hostap_iface_unused_stack` | always |
| `wpa_s_work_q` | `ncs_wifi_wpa_supp_unused_stack` | always |
| `sysworkq` | `ncs_wifi_wpa_supp_unused_stack` (shared) | always |
| `k_system_thread` | `ncs_wifi_wpa_supp_unused_stack` (shared) | always |
| `bt_rx` | `ncs_bt_rx_unused_stack` | `CONFIG_BT` |
| `bt_tx` | `ncs_bt_tx_unused_stack` | `CONFIG_BT` |
| `bt_hci_isr` | `ncs_bt_hci_isr_unused_stack` | `CONFIG_BT` |
| `bt_host_tx` | `ncs_bt_host_tx_unused_stack` | `CONFIG_BT` |
| `ntp` | `ncs_ntp_unused_stack` | `CONFIG_NTP_MODULE` |

---

## ZView Support

When `CONFIG_ZEGO_MEMONITOR_ZVIEW=y`, all tracked heaps and threads are visible
in the ZView TUI via JLink SWD — no firmware reboot required.

`CONFIG_STACK_SENTINEL=n` is required; `STACK_SENTINEL` corrupts stack HWM reads.

ZView command (nRF7002DK):
```bash
west zview live \
  -e build_nrf7002dk/nordic-wifi-memfault/zephyr/zephyr.elf \
  -r jlink \
  -t nRF5340_xxAA \
  -s <serial_number>
```

Note: ZView discovers `k_heap` typed variables from DWARF. Variables present in
DWARF but not linked (e.g. `net_buf_mem_pool_rx_bufs` when
`CONFIG_NET_BUF_FIXED_DATA_SIZE=y`) are silently skipped; they do not cause an error
(fix applied in `modules/tools/zview/src/orchestrator.py`).

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| Heap stats unavailable | API guard | skip that heap entry |
| Thread list empty | count == 0 | no-op; heaps still sampled |
| Memfault metric key not found | name not in lookup table | silent skip |
| ZView symbol not linked in ELF | `LookupError` in orchestrator | skip gracefully (patched) |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | Zbus infrastructure + optional LOG_PERIODIC format strings |
| RAM (static) | ~2 KB | Static heap/thread cache arrays in brick |
| Stack | workqueue context | no dedicated thread; delayed work in system workqueue |
| App subscriber | file-scope static buffers | avoids subscriber stack pressure |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | `[memonitor] memonitor initialized` | appears once at boot |
| Periodic sample (LOG_PERIODIC=y) | `heap %-24s hwm=…/… (N%)` + `thrd %-24s hwm=…/… (N%)` | emitted every `INTERVAL_MS` |
| Memfault heartbeat | thread stack metric updated in Memfault dashboard | numeric value ≠ 0 |
| ZView connect | heap bars visible in TUI | heaps update on each interval |
| ZView thread tab | thread watermark bars visible | watermarks non-zero after exercise |
