# App Wi-Fi Provisioning over BLE Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | wifi_prov_over_ble |
| Version | 2026-05-14-14-13 |
| PRD Version | 2026-05-14-14-13 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/wifi_prov_over_ble implementation |

---

## Overview

This module wraps Nordic Wi-Fi provisioning service over BLE to allow mobile app
credential onboarding. Credentials are stored persistently and used by the
network module for automatic reconnection on later boots.

---

## Location

- Path: src/modules/wifi_prov_over_ble/
- Files: wifi_prov_over_ble.c, wifi_prov_over_ble.h, Kconfig.wifi_prov_over_ble, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Library wrapper module

---

## External Library Interface

| Field | Value |
|-------|-------|
| Library | Nordic Wi-Fi provisioning over BLE service |
| NCS Kconfig | CONFIG_BT_WIFI_PROV and selected dependencies via CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED |
| Library internal threads | BLE/controller internal workqueues |

APIs called by this module include BLE/provisioning start-stop lifecycle and
provisioning callback registration provided by NCS provisioning libraries.

Callbacks implemented handle provisioning status transitions and credential
write completion.

Zbus integration:
- Subscribes to WIFI_CHAN for connectivity state changes to adjust advertising and user feedback behavior.

---

## Zbus Integration

Subscribes to WIFI_CHAN and reacts to:
- WIFI_STA_CONNECTED
- WIFI_STA_DISCONNECTED

Does not publish a dedicated channel in current implementation.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_WIFI_STA_PROV_OVER_BLE_ENABLED | bool | n (enabled from prj.conf) | Enable BLE provisioning wrapper |

---

## API / Public Interface

- wifi_prov_over_ble_init() registered by SYS_INIT.
- Internal callbacks bridge provisioning events to logs and system settings.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| BLE stack init failure | return code on init | error log and disable provisioning flow |
| Provisioning failure | callback status | error log and keep device in provisioning state |
| Invalid credentials | connect failure after provision | disconnect path + user retries provisioning |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | BLE + provisioning glue |
| RAM (static) | medium | BLE service runtime overhead |
| Stack | system workqueues | no dedicated app thread |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | provisioning module init log | BLE advertising visible |
| Successful provisioning | credential write success log | next connect succeeds without reprovision |
| Reboot persistence | reconnect logs on reboot | credentials retained in settings storage |
