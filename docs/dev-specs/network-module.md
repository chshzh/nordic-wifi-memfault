# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | network |
| Version | 2026-05-14-14-13 |
| PRD Version | 2026-05-14-14-13 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/network implementation |

---

## Overview

The network module owns Wi-Fi and IP-layer event handling and publishes runtime
connectivity state through WIFI_CHAN and NETWORK_CHAN. It is the primary source
of connectivity truth for dependent modules.

---

## Location

- Path: src/modules/network/
- Files: net_event_mgmt.c, net_event_mgmt.h, wifi_utils.c, wifi_utils.h, Kconfig, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Application module

---

## Zbus Integration

Publishes:
- WIFI_CHAN (struct wifi_msg)
- NETWORK_CHAN (struct network_msg)

No channel subscriptions.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_WIFI_MODULE | bool | y | Enable Wi-Fi/network event module |
| CONFIG_WIFI_MODULE_INIT_PRIORITY | int | 90 | SYS_INIT priority for network init |

---

## API / Public Interface

- net_event_mgmt_init()/init_network_events() initialize net_mgmt callbacks and zbus channels.
- wifi_utils helpers provide Wi-Fi info retrieval used by other modules.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| net_mgmt callback registration failure | return code check | error log and init failure path |
| zbus publish failure | return from zbus_chan_pub | warning log and continue |
| connection drop | NET_EVENT_L4_DISCONNECTED | publish disconnect and rely on connection manager retry |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | event handling and utility logic |
| RAM (static) | low | event state |
| Stack | callback context | no dedicated thread in module |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | [wifi] WiFi module initialized | module ready at boot |
| L4 connected | connected/network-ready logs | WIFI_CHAN and NETWORK_CHAN publish succeeds |
| L4 disconnected | disconnected log | disconnect event published |
