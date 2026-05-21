# Button Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | button |
| Version | 2026-05-14-14-13 |
| PRD Version | 2026-05-14-14-13 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/button implementation |

---

## Overview

The button module converts DK button interactions into normalized event messages
on BUTTON_CHAN. It is the user input source for heartbeat/OTA triggers and crash
demo paths handled by app_memfault listeners.

---

## Location

- Path: src/modules/button/
- Files: button.c, button.h, Kconfig.button, CMakeLists.txt

---

## Module Type

- Application module

---

## Zbus Integration

Publishes to BUTTON_CHAN with struct button_msg.

```c
struct button_msg {
    enum button_msg_type type;
    uint8_t button_number;
    uint32_t duration_ms;
    uint32_t press_count;
    uint32_t timestamp;
};
```

No channel subscriptions.

---

## State Machine

The module behavior is event-driven from button callbacks, with logical states:
Idle -> Pressed -> Released for each button event stream.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_BUTTON_MODULE | bool | y | Enable button module |
| CONFIG_BUTTON_LONG_PRESS_MS | int | 3000 | Long-press threshold in milliseconds |

---

## API / Public Interface

Public APIs are defined in button.h and consumed via module init and zbus events.
There is no required direct cross-module API for normal operation.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| zbus publish failure | return code from zbus_chan_pub | warning log and continue |
| Invalid button index | callback input validation | ignore event |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | small | GPIO handling and event mapping |
| RAM (static) | small | counters and state |
| Stack | callback context | no dedicated thread |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Module init | [button] initialized | appears on boot |
| Short press button 1 | button release event log | app_memfault heartbeat trigger path runs |
| Short press button 2 | button release event log | OTA trigger path runs |
| Long press threshold | duration_ms >= CONFIG_BUTTON_LONG_PRESS_MS | crash demo action invoked by subscriber |
