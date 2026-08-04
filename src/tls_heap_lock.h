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

#endif /* TLS_HEAP_LOCK_H */
