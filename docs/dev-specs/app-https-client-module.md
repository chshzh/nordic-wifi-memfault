# App HTTPS Client Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | app_https_client |
| Version | 2026-05-16-13-00 |
| PRD Version | 2026-05-14-14-13 |
| Author | GitHub Copilot |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-14-14-13 | Reverse-design spec created from src/modules/app_https_client implementation |
| 2026-05-16-13-00 | Per-request GET log demoted to DBG; new INF summary log `Test Result: N/N (success/total)` emitted after each request |

---

## Overview

app_https_client runs periodic HTTPS HEAD requests against a configured hostname
and reports success/failure counters for observability.

---

## Location

- Path: src/modules/app_https_client/
- Files: app_https_client.c, app_https_client.h, Kconfig.app_https_client, Kconfig.defaults, CMakeLists.txt

---

## Module Type

- Application module

---

## Zbus Integration

Subscribes to WIFI_CHAN using listener registration. Connectivity state controls
when request loops are active.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| CONFIG_APP_HTTPS_CLIENT_MODULE | bool | n (enabled from prj.conf) | Enable HTTPS client module |
| CONFIG_APP_HTTPS_HOSTNAME | string | module default/overridden | Target HTTPS host |
| CONFIG_APP_HTTPS_REQUEST_INTERVAL_SEC | int | module default/overridden | Request interval |
| CONFIG_APP_HTTPS_CLIENT_STACK_SIZE | int | module default/overridden | Worker thread stack |
| CONFIG_APP_HTTPS_CLIENT_THREAD_PRIORITY | int | module default/overridden | Worker priority |

---

## API / Public Interface

- app_https_client_module_init() via SYS_INIT.
- Dedicated thread executes periodic requests based on connectivity state.

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| DNS resolution failure | socket/TLS connect path | count failure and retry next interval |
| TLS/HTTP failure | request return/error codes | count failure and continue loop |
| Wi-Fi disconnect | WIFI_CHAN event | pause/skip requests until reconnect |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | medium | TLS/socket/http logic |
| RAM (static) | low | request state |
| Stack | dedicated thread | CONFIG_APP_HTTPS_CLIENT_STACK_SIZE |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Init | app_https_client init log | module starts |
| Connected operation | `<inf> app_https_client: Test Result: N/N (success/total)` after each request; per-request `GET <host> -> <status>  (total=N, fail=N)` at `<dbg>` only | counter progression visible at INF; detail available at DBG |
| Disconnect handling | stop/pause behavior logs | no hard fault on link loss |
