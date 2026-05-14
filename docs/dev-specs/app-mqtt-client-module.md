# App MQTT Client Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | app_mqtt_client |
| Version | 2026-05-14-14-13 |
| PRD Version | 2026-05-14-14-13 |
| Author | GitHub Copilot |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/app_mqtt_client implementation |

---

## Overview

app_mqtt_client provides a TLS MQTT echo workflow for connectivity validation
and telemetry counters. It uses mqtt_helper and reacts to Wi-Fi state via zbus.

---

## Location

- Path: src/modules/app_mqtt_client/
- Files: app_mqtt_client.c, app_mqtt_client.h, Kconfig.app_mqtt_client, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Library wrapper module

---

## External Library Interface

| Field | Value |
|-------|-------|
| Library | mqtt_helper |
| NCS Kconfig | CONFIG_MQTT_HELPER (selected by APP_MQTT_CLIENT_MODULE) |
| Library internal threads | mqtt_helper thread/workqueue |

Wrapper module responsibilities:
- configure broker/topic/client identity,
- handle reconnect/publish timing,
- map success/failure to metrics and logs.

---

## Zbus Integration

Subscribes to WIFI_CHAN using listener registration.
On disconnect events, publish activity is suspended until connectivity returns.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_APP_MQTT_CLIENT_MODULE | bool | n (enabled from prj.conf) | Enable MQTT client module |
| CONFIG_APP_MQTT_CLIENT_BROKER_HOSTNAME | string | module default/overridden | Broker hostname |
| CONFIG_APP_MQTT_CLIENT_PUBLISH_TOPIC | string | module default/overridden | Topic suffix |
| CONFIG_APP_MQTT_CLIENT_PUBLISH_INTERVAL_SEC | int | module default/overridden | Publish interval |
| CONFIG_APP_MQTT_CLIENT_RECONNECT_TIMEOUT_SEC | int | module default/overridden | Retry timeout |
| CONFIG_APP_MQTT_CLIENT_STACK_SIZE | int | module default/overridden | Worker stack size |

---

## API / Public Interface

- app_mqtt_client_module_init() via SYS_INIT.
- Dedicated thread handles broker lifecycle and publish loop.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| Broker connect failure | mqtt_helper return/error callback | retry after timeout |
| Publish failure | publish return code | count failure and continue |
| TLS credential issue | connection setup failure | log and retry |
| Wi-Fi disconnect | WIFI_CHAN event | pause client operations |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | MQTT + TLS glue |
| RAM (static) | medium | client buffers/context |
| Stack | dedicated thread + mqtt_helper thread | tuned in prj.conf |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | app_mqtt_client init log | module starts |
| Connected operation | periodic publish/echo logs | counter progression visible |
| Link loss | reconnect/disconnect logs | clean recovery without crash |
