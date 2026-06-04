# Button Module

> **Provided by zego library.**
>
> The button module is provided by `zego/modules/button` (registered via
> `EXTRA_ZEPHYR_MODULES` in `CMakeLists.txt`). There is no local
> `src/modules/button/` in this project.
>
> See the canonical spec:
> [`zego/modules/button/docs/button-spec.md`](../../../../zego/modules/button/docs/button-spec.md)

## Integration in this project

| Item | Value |
|------|-------|
| Source | `zego/modules/button/` |
| Registered as | `EXTRA_ZEPHYR_MODULES` in `CMakeLists.txt` |
| Enable | `CONFIG_ZEGO_BUTTON=y` |
| Board: nRF7002DK | `CONFIG_ZEGO_BUTTON_NUM_BUTTONS=2` |
| Board: nRF54LM20DK | `CONFIG_ZEGO_BUTTON_NUM_BUTTONS=3` |
| Channel published | `BUTTON_CHAN` (`struct button_msg`) |
| Consumer | `src/modules/app_memfault/core/app_memfault_core.c` (Button 1 → heartbeat/CDR, Button 2 → OTA/crash demo) |
