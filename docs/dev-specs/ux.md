# UX Module Engineering Spec — LED Wi-Fi Feedback

## Document Information

| Field | Value |
|---|---|
| Module | app_ux |
| Version | 2026-06-04-23-00 |
| PRD Version | 2026-05-22-10-00 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-23-00 | Initial spec — LED-only Wi-Fi state feedback using zego/led |

---

## 1. Purpose

The UX module (`src/modules/ux/ux.c`) drives LED 0 to reflect Wi-Fi connectivity
state using `LED_CMD_CHAN` (from `zego/led`).

Enable with `CONFIG_APP_UX_MODULE=y`.  No button handling — button gestures (heartbeat,
OTA check, crash demo) are handled by `app_memfault_core.c` via `BUTTON_CHAN`.

---

## 2. Kconfig Symbols

| Symbol | Default | Description |
|--------|---------|-------------|
| `CONFIG_APP_UX_MODULE` | `n` | Enable the UX module |
| `CONFIG_APP_UX_INIT_PRIORITY` | `95` | `SYS_INIT` priority; must be > `ZEGO_LED_INIT_PRIORITY` (91) |

---

## 3. LED 0 State Machine

LED 0 is driven by `APP_WIFI_STATE_CHAN` (published by `net_event_app.c`).

```
Boot
 │
 ▼
[MARQUEE] ◄── SYS_INIT (APP_UX priority 95)
 │
 ├──► APP_WIFI_STATE_CONNECTED  ──► [Solid ON]
 │
 └──► APP_WIFI_STATE_ERROR      ──► [Fast BLINK 100 ms]
              │
              └──► APP_WIFI_STATE_CONNECTED ──► [Solid ON]
```

| `app_wifi_state` | LED 0 effect | `period_ms` |
|------------------|-------------|-------------|
| `CONNECTING` (boot) | MARQUEE | Kconfig default |
| `CONNECTED` | Solid ON | — |
| `ERROR` (disconnected) | BLINK | 100 ms |

---

## 4. APP_WIFI_STATE_CHAN

Defined in `src/modules/network/net_event_app.c`. Declared in `src/modules/messages.h`.

| Event | Published when |
|-------|----------------|
| `APP_WIFI_STATE_CONNECTED` | `zego_network_on_wifi_connected()` fires (STA connected) |
| `APP_WIFI_STATE_ERROR` | `zego_network_on_wifi_disconnected()` fires |

> `APP_WIFI_STATE_CONNECTING` is not published explicitly — the UX module starts
> MARQUEE at boot via `SYS_INIT` unconditionally.

---

## 5. Init-Order Safety

The atomic `app_ux_ready` flag (set in `app_ux_init()`, SYS_INIT priority 95) ensures
that `app_ux_led_work_fn` does not call `k_work_schedule()` on `led_sm[n].effect_work`
before `k_work_init_delayable()` has run in `led_module_init()` (priority 91).

Any connectivity event that arrives before init completes is replayed from `app_ux_init()`.

---

## 6. Zbus Channel Summary

| Channel | Role | Direction |
|---------|------|-----------|
| `APP_WIFI_STATE_CHAN` | Wi-Fi state events (from `net_event_app.c`) | Input |
| `LED_CMD_CHAN` | LED commands (to `zego/led`) | Output |

---

## 7. File Map

| File | Role |
|------|------|
| `src/modules/ux/ux.c` | Module implementation |
| `src/modules/ux/Kconfig` | `CONFIG_APP_UX_MODULE`, `CONFIG_APP_UX_INIT_PRIORITY` |
| `src/modules/ux/CMakeLists.txt` | Adds `ux.c` when module is enabled |
| `src/modules/messages.h` | `APP_WIFI_STATE_CHAN` declaration + `app_wifi_state_msg` type |
| `src/modules/network/net_event_app.c` | Defines `APP_WIFI_STATE_CHAN`; publishes on connectivity events |
