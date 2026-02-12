# System Architecture

## Overview

Memfault nRF7002DK uses a modular, event-driven architecture based on:
- **State Machine Framework (SMF)** for stateful modules
- **Zbus** for inter-module messaging
- **SYS_INIT** for priority-based initialization

## Architecture Pattern

**Selected**: SMF + Zbus Modular Architecture

**Rationale**:
- Loose coupling between modules (easier to add/remove features)
- Message-based communication simplifies testing
- State machines provide verifiable behavior
- Industry best practice for production IoT systems

**Trade-offs**:
- ~10 KB higher overhead vs simple threading
- Requires SMF/Zbus knowledge
- More initial setup complexity
- **Worth it for**: Maintainability, testability, scalability

## System Diagram

```
┌───────────────────────────────────────────────────────────┐
│                     Zbus Message Bus                       │
│  ┌──────────────┐              ┌──────────────┐           │
│  │ BUTTON_CHAN  │              │  WIFI_CHAN   │           │
│  └──────────────┘              └──────────────┘           │
└───────────────────────────────────────────────────────────┘
         ▲                                ▲
         │ publish                        │ publish
         │                                │
┌────────┴──────┐                ┌───────┴────────┐
│    Button     │                │      WiFi      │
│    Module     │                │     Module     │
│   (SMF-based) │                │ (Event-driven) │
└───────────────┘                └────────────────┘
         │                                │
         │ subscribe                      │ subscribe
         ▼                                ▼
┌───────────────────────────────────────────────────────────┐
│  Memfault Core │ WiFi Prov BLE │ HTTPS │ MQTT │ OTA Trig │
└───────────────────────────────────────────────────────────┘
```

## Zbus Channels

### BUTTON_CHAN

**Type**: `struct button_msg`
```c
struct button_msg {
    enum button_msg_type type;  // BUTTON_PRESSED or BUTTON_RELEASED
    uint8_t button_number;       // 1-4
    uint32_t duration_ms;        // Press duration
    uint32_t press_count;        // Total presses since boot
    uint32_t timestamp;          // k_uptime_get_32()
};
```

**Publishers**: Button Module  
**Subscribers**: Memfault Core Module

**Messages**:
- `BUTTON_RELEASED` with button number, duration, count

---

### WIFI_CHAN

**Type**: `struct wifi_msg`
```c
struct wifi_msg {
    enum wifi_msg_type type;  // WIFI_STA_CONNECTED, WIFI_STA_DISCONNECTED
    int32_t rssi;             // Signal strength (future use)
    int error_code;           // Error details (if applicable)
};
```

**Publishers**: WiFi Module  
**Subscribers**: Memfault Core, WiFi Prov BLE, HTTPS Client, MQTT Client, OTA Triggers

**Messages**:
- `WIFI_STA_CONNECTED` - L4 network ready (IP assigned)
- `WIFI_STA_DISCONNECTED` - Network lost

## Module Initialization Order

Uses `SYS_INIT` with priorities (lower number = earlier init):

| Priority | Module | Rationale |
|----------|--------|-----------|
| 0 | WiFi Prov over BLE | BLE stack must start early |
| 1 | WiFi Module | Network layer before consumers |
| 2 | Button Module | GPIO can init anytime |
| 3 | Memfault Core | Needs network events |
| 4 | HTTPS/MQTT Clients | Depends on network |

## Key Design Decisions

### Why Delayed Boot Connect (1s)?

WiFi module waits 1 second after `SYS_INIT` before connecting to ensure:
- All modules initialized (including subsystems)
- Memfault SDK ready to capture early crashes
- BLE advertising started (for WiFi provisioning)

### Why DNS Wait Before Upload?

Memfault Core waits for DNS resolution of `chunks-nrf.memfault.com` (up to 300s) before data upload to avoid:
- Early upload failures (DNS not ready yet)
- Unnecessary retry overhead
- Flash wear from failed attempts

Timeout ensures uploads proceed even if DNS is slow.

### Why Event-Driven WiFi (Not SMF)?

WiFi module uses Zephyr's Connection Manager callbacks instead of SMF because:
- Network stack is inherently event-driven
- No need for state machine complexity
- Direct callbacks are more efficient
- Publishes to Zbus on L4 connected/disconnected only

## Module Dependencies

```
Button Module:
  - DK Library (GPIO)
  - Zbus

WiFi Module:
  - WiFi driver
  - WPA Supplicant
  - Connection Manager
  - Zbus

Memfault Core:
  - Memfault SDK
  - Zbus (BUTTON_CHAN, WIFI_CHAN)
  - DNS resolver

WiFi Prov over BLE:
  - BLE stack
  - WiFi Provisioning Service
  - Zbus (WIFI_CHAN)

HTTPS/MQTT Clients:
  - TLS
  - HTTP/MQTT library
  - Zbus (WIFI_CHAN)
```

## Memory Footprint

| Component | Flash | RAM |
|-----------|-------|-----|
| Base (WiFi + Memfault) | ~750 KB | ~380 KB |
| +WiFi Prov BLE | +45 KB | +20 KB |
| +HTTPS Client | +30 KB | +8 KB |
| +MQTT Client | +25 KB | +12 KB |
| +nRF70 Stats CDR | +15 KB | +4 KB |
| **Total (all features)** | **~865 KB** | **~424 KB** |

See [README.md Flash Memory Layout](../README.md#flash-memory-layout) for partition details.

## Related Specs

- [button-module.md](button-module.md) - Button SMF implementation
- [wifi-module.md](wifi-module.md) - WiFi connectivity
- [memfault-integration.md](memfault-integration.md) - Cloud integration
