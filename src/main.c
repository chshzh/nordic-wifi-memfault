/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <ux.h>
#include <zephyr/logging/log.h>

#include "console.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

/* Non-zego (app-specific) modules compiled into this image. See
 * zego/bricks/ux/docs/ux-spec.md §9 for the banner_compiled_app_modules() API. */
void banner_compiled_app_modules(void)
{
	LOG_INF("----------------------------------------------");
	LOG_INF("APP:");
#if CONFIG_APP_MEMFAULT_MODULE
	LOG_INF("  " CLR_BLU "app_memfault" CLR_RST);
#endif
#if CONFIG_APP_HTTPS_CLIENT_MODULE
	LOG_INF("  " CLR_BLU "app_https_client" CLR_RST);
#endif
#if CONFIG_APP_MQTT_CLIENT_MODULE
	LOG_INF("  " CLR_BLU "app_mqtt_client" CLR_RST);
#endif
}

int main(void)
{
	zego_ux_print_banner();
	return 0;
}
