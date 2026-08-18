/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef TLS_HEAP_LOCK_H
#define TLS_HEAP_LOCK_H

#include <zephyr/kernel.h>

/*
 * CONFIG_MBEDTLS_THREADING_C is unavailable without CC3XX on this NCS
 * version (nrf_security only defaults it on for CryptoCell-equipped SoCs),
 * so mbedTLS's shared static heap (CONFIG_MBEDTLS_ENABLE_HEAP) is not
 * thread-safe here. Every module that opens/closes a TLS connection must
 * hold this mutex around the connect (handshake) and close (teardown)
 * sequence to avoid concurrent mbedtls_calloc()/mbedtls_free() calls
 * corrupting the shared heap's free list.
 */
extern struct k_mutex tls_heap_lock;

/*
 * Ceiling on how long a caller waits for the lock before giving up on this
 * cycle. Waiting K_FOREVER instead lets one wedged TLS session freeze every
 * other module in the application permanently, with no way out but a power
 * cycle. A legitimate holder needs at most one TLS session (DNS, handshake,
 * request, response -- each bounded by its own 30 s socket timeout), so this
 * cannot fire on a healthy device. The exception is an OTA download, which
 * holds the lock for as long as the transfer takes; skipping an HTTPS or MQTT
 * cycle during an OTA is the intended trade.
 */
#define TLS_HEAP_LOCK_TIMEOUT K_SECONDS(180)

#endif /* TLS_HEAP_LOCK_H */
