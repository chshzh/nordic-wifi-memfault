# System Architecture Specification - nordic-wifi-memfault

## Document Information

| Field | Value |
|-------|-------|
| Project | nordic-wifi-memfault |
| Version | 2026-05-14-14-13 |
| PRD Version | 2026-05-14-14-13 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design architecture baseline generated from implementation |
| 2026-05-15-10-31 | Add app_memfault core as NETWORK_CHAN subscriber |

---

## Overview

Application architecture is modular and event-driven. Modules initialize through
SYS_INIT and communicate through zbus channels using shared message types from
src/modules/messages.h. Connectivity and cloud interactions are implemented as
wrappers around Zephyr/NCS subsystems and Memfault SDK APIs.

Scope note: this architecture governs application modules only. External SDKs
(Memfault, Wi-Fi driver, BLE provisioning internals, TLS stack) may run their
own internal threads.

---

## Module Map

```
src/
├── main.c
└── modules/
    ├── messages.h
    ├── button/
    ├── network/
    ├── heap_monitor/
    ├── wifi_prov_over_ble/
    ├── app_memfault/
    │   ├── core/
    │   ├── metrics/
    │   ├── ota/
    │   ├── cdr/
    │   └── config/
    ├── app_https_client/
    ├── app_mqtt_client/
    └── ntp/
```

---

## Zbus Channels

| Channel | Message Type | Publisher | Subscribers | Direction |
|---------|-------------|-----------|-------------|-----------|
| BUTTON_CHAN | struct button_msg | button module | app_memfault core, app_memfault ota, app_memfault cdr | runtime |
| WIFI_CHAN | struct wifi_msg | network module | app_memfault core, app_memfault ota, wifi_prov_over_ble, app_https_client, app_mqtt_client | runtime |
| NETWORK_CHAN | struct network_msg | network module | app_memfault core, ntp (optional) | runtime |

### Message Definitions

```c
enum button_msg_type { BUTTON_PRESSED, BUTTON_RELEASED };
struct button_msg {
    enum button_msg_type type;
    uint8_t button_number;
    uint32_t duration_ms;
    uint32_t press_count;
    uint32_t timestamp;
};

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

enum network_msg_type { NETWORK_READY, NETWORK_NOT_READY };
struct network_msg {
    enum network_msg_type type;
    bool ready;
};
```

---

## External Libraries

| Library | NCS Kconfig | Internal threads | App wrapper module |
|---------|-------------|------------------|--------------------|
| Memfault SDK | CONFIG_MEMFAULT | Memfault HTTP/FOTA worker threads | app_memfault/ |
| Wi-Fi Provisioning Service | CONFIG_BT_WIFI_PROV | BLE/controller workqueues | wifi_prov_over_ble/ |
| MQTT Helper | CONFIG_MQTT_HELPER | Helper-managed MQTT context threads | app_mqtt_client/ |

---

## Boot Sequence

| Priority | Module | SYS_INIT call | UART marker |
|----------|--------|---------------|-------------|
| 90 (default) | network | init_network_events | [wifi] WiFi module initialized |
| default app init prio | button | button_module_init | [button] initialized |
| default kernel init prio | heap_monitor | heap_monitor_init | [heap_monitor] enabled |
| default app init prio | app_memfault core | memfault_core_init | [app_memfault] initialized |
| 2 | app_https_client | app_https_client_module_init | [app_https_client] initialized |
| 2 | app_mqtt_client | app_mqtt_client_module_init | [app_mqtt_client] initialized |
| 95 | wifi_prov_over_ble | wifi_prov_over_ble_init | [wifi_prov_over_ble] initialized |

---

## Thread Budget

| Thread | Stack symbol | Purpose |
|--------|--------------|---------|
| memfault_upload_tid | CONFIG_MEMFAULT_UPLOAD_THREAD_STACK_SIZE | On-connect DNS wait and upload trigger |
| mflt_ota_triggers_tid | MFLT_OTA_TRIGGERS_THREAD_STACK_SIZE | Periodic and event-triggered OTA check scheduling |
| app_https_client_tid | CONFIG_APP_HTTPS_CLIENT_STACK_SIZE | Periodic HTTPS HEAD requests |
| app_mqtt_client_tid | CONFIG_APP_MQTT_CLIENT_STACK_SIZE | MQTT connect/publish loop |

---

## Memory Budget

| Area | Current note |
|------|--------------|
| Stack tuning | Thread stacks are tuned in prj.conf from measurements (see comments) |
| System heap | CONFIG_HEAP_MEM_POOL_SIZE=72410 with headroom based on measured peak |
| mbedTLS heap | CONFIG_MBEDTLS_HEAP_SIZE=86880 |
| Coredump storage | Board-specific partition in DTS overlays |

For full partition maps, PM-to-DTS migration rationale, and OTA compatibility
constraints, see [flash-memory-layout.md](flash-memory-layout.md).

---

## Test Points

| Stage | UART log expected | Pass condition |
|-------|-------------------|----------------|
| Boot banner | Board/Version/Enabled modules lines from main.c | Appears each boot |
| Connectivity up | WiFi CONNECTED / network ready log | Device has IP and publishes WIFI_CHAN |
| Memfault trigger | Upload or heartbeat logs after connect/button action | Events observed in logs/dashboard |
| Optional clients | HTTPS/MQTT periodic success/fail logs | Metrics counters progress |
