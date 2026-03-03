/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NRF70_FW_STATS_CDR_H
#define NRF70_FW_STATS_CDR_H

#include <stddef.h>

int mflt_nrf70_fw_stats_cdr_init(void);
int mflt_nrf70_fw_stats_cdr_collect(void);
size_t mflt_nrf70_fw_stats_cdr_get_size(void);

#endif /* NRF70_FW_STATS_CDR_H */
