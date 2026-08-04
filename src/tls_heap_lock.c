/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "tls_heap_lock.h"

K_MUTEX_DEFINE(tls_heap_lock);
