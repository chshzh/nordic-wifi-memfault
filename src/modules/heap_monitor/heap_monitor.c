// SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if CONFIG_HEAP_MEM_POOL_SIZE > 0
#include <zephyr/sys/heap_listener.h>
#include <zephyr/sys/mem_stats.h>
#endif

#if defined(CONFIG_MBEDTLS_ENABLE_HEAP) && defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
#include <mbedtls/memory_buffer_alloc.h>
#endif

#if defined(CONFIG_APP_MEMFAULT_MODULE)
#include <memfault/metrics/metrics.h>
#else
#define MEMFAULT_METRIC_SET_UNSIGNED(...) ((void)0)
#endif

LOG_MODULE_REGISTER(heap_monitor, CONFIG_HEAPS_MONITOR_LOG_LEVEL);

#if CONFIG_HEAP_MEM_POOL_SIZE > 0
/*
 * _system_heap is defined by Zephyr kernel (kernel/mempool.c) but intentionally
 * not exported in a public header. We access it only when the application uses
 * the system heap.
 */
extern struct k_heap _system_heap;

static uint32_t system_last_reported_high;
static uint32_t system_last_warn_pct;
static bool system_boot_report_pending = true;
#endif

#if defined(CONFIG_MBEDTLS_ENABLE_HEAP) && defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
static uint32_t mbedtls_peak_used_all_time;
static size_t mbedtls_peak_blocks_all_time;
#endif

static void log_heap_line(const char *heap_name, uint32_t used, uint32_t total,
			  uint32_t peak, bool have_blocks, size_t blocks,
			  size_t peak_blocks, bool warn)
{
	const uint32_t used_pct = total ? (used * 100U) / total : 0U;
	const uint32_t peak_pct = total ? (peak * 100U) / total : 0U;

	if (have_blocks) {
		if (warn) {
			LOG_WRN("%s Heap: used=%u/%u (%u%%) blocks=%zu, peak=%u/%u (%u%%), peak_blocks=%zu",
				heap_name, used, total, used_pct, blocks, peak,
				total, peak_pct, peak_blocks);
		} else {
			LOG_INF("%s Heap: used=%u/%u (%u%%) blocks=%zu, peak=%u/%u (%u%%), peak_blocks=%zu",
				heap_name, used, total, used_pct, blocks, peak,
				total, peak_pct, peak_blocks);
		}
	} else {
		if (warn) {
			LOG_WRN("%s Heap: used=%u/%u (%u%%) blocks=n/a, peak=%u/%u (%u%%), peak_blocks=n/a",
				heap_name, used, total, used_pct, peak, total,
				peak_pct);
		} else {
			LOG_INF("%s Heap: used=%u/%u (%u%%) blocks=n/a, peak=%u/%u (%u%%), peak_blocks=n/a",
				heap_name, used, total, used_pct, peak, total,
				peak_pct);
		}
	}
}

#if CONFIG_HEAP_MEM_POOL_SIZE > 0
static void update_system_heap_metrics(uint32_t total, uint32_t used, uint32_t peak)
{
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_system_heap_total, total);
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_system_heap_used, used);
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_system_heap_peak, peak);
}

static int get_system_heap_stats(struct sys_memory_stats *stats)
{
	return sys_heap_runtime_stats_get((struct sys_heap *)&_system_heap.heap, stats);
}

static void report_system_heap_peak_if_needed(void)
{
	struct sys_memory_stats stats;

	if (get_system_heap_stats(&stats) != 0) {
		return;
	}

	const uint32_t total =
		(uint32_t)(stats.allocated_bytes + stats.free_bytes);
	if (total == 0U) {
		return;
	}

	const uint32_t peak = (uint32_t)stats.max_allocated_bytes;
	const uint32_t used = (uint32_t)stats.allocated_bytes;
	const uint32_t peak_pct = (peak * 100U) / total;
	const bool warn = peak_pct >= CONFIG_HEAPS_MONITOR_WARN_PCT;
	bool progressed = false;

	if (peak > system_last_reported_high) {
		progressed =
			(peak - system_last_reported_high) >= CONFIG_HEAPS_MONITOR_STEP_BYTES;
	}

	const bool new_warn = warn && (peak_pct > system_last_warn_pct);
	const bool should_report =
		progressed || new_warn || system_boot_report_pending;

	if (!should_report) {
		return;
	}

	system_last_reported_high = MAX(system_last_reported_high, peak);
	system_boot_report_pending = false;

	if (warn) {
		system_last_warn_pct = peak_pct;
	}

	log_heap_line("system", used, total, peak, false, 0U, 0U, warn);
	update_system_heap_metrics(total, used, peak);
}

static void heap_listener_alloc(uintptr_t heap_id, void *mem, size_t bytes)
{
	ARG_UNUSED(mem);
	ARG_UNUSED(bytes);

	if (heap_id != HEAP_ID_FROM_POINTER(&_system_heap)) {
		return;
	}

	report_system_heap_peak_if_needed();
}

HEAP_LISTENER_ALLOC_DEFINE(heap_monitor_listener_alloc,
			   HEAP_ID_FROM_POINTER(&_system_heap),
			   heap_listener_alloc);
#endif

#if defined(CONFIG_MBEDTLS_ENABLE_HEAP) && defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
static void update_mbedtls_heap_metrics(uint32_t total, uint32_t used, uint32_t peak)
{
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_mbedtls_heap_total, total);
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_mbedtls_heap_used, used);
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_mbedtls_heap_peak, peak);
}

static void report_mbedtls_heap(bool warn_on_used_pct)
{
	size_t cur_used;
	size_t cur_blocks;
	size_t max_used;
	size_t max_blocks;
	const uint32_t total = CONFIG_MBEDTLS_HEAP_SIZE;

	mbedtls_memory_buffer_alloc_cur_get(&cur_used, &cur_blocks);
	mbedtls_memory_buffer_alloc_max_get(&max_used, &max_blocks);

	mbedtls_peak_used_all_time =
		MAX(mbedtls_peak_used_all_time, (uint32_t)max_used);
	mbedtls_peak_blocks_all_time =
		MAX(mbedtls_peak_blocks_all_time, max_blocks);

	update_mbedtls_heap_metrics(total, (uint32_t)cur_used,
				    mbedtls_peak_used_all_time);

	const bool warn = warn_on_used_pct && (total != 0U) &&
			  (((uint32_t)cur_used * 100U) / total >=
			   CONFIG_HEAPS_MONITOR_WARN_PCT);

	log_heap_line("mbedTLS", (uint32_t)cur_used, total,
		      mbedtls_peak_used_all_time, true, cur_blocks,
		      mbedtls_peak_blocks_all_time, warn);
}
#endif

static void periodic_heap_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(periodic_heap_work, periodic_heap_work_fn);

static void periodic_heap_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

#if CONFIG_HEAP_MEM_POOL_SIZE > 0
	{
		struct sys_memory_stats stats;

		if (get_system_heap_stats(&stats) == 0) {
			const uint32_t total =
				(uint32_t)(stats.allocated_bytes + stats.free_bytes);
			const uint32_t used = (uint32_t)stats.allocated_bytes;
			const uint32_t peak = (uint32_t)stats.max_allocated_bytes;
			const bool warn = total != 0U &&
					  ((used * 100U) / total >=
					   CONFIG_HEAPS_MONITOR_WARN_PCT);

			log_heap_line("System", used, total, peak, false, 0U, 0U, warn);
			update_system_heap_metrics(total, used, peak);
		}
	}
#endif

#if defined(CONFIG_MBEDTLS_ENABLE_HEAP) && defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
	report_mbedtls_heap(true);
#endif

	k_work_reschedule(&periodic_heap_work,
			  K_SECONDS(CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC));
}

static int heap_monitor_init(void)
{
#if CONFIG_HEAP_MEM_POOL_SIZE > 0
	report_system_heap_peak_if_needed();
#endif

#if defined(CONFIG_MBEDTLS_ENABLE_HEAP) && defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
	report_mbedtls_heap(false);
#endif

	k_work_schedule(&periodic_heap_work,
			K_SECONDS(CONFIG_HEAPS_MONITOR_PERIODIC_INTERVAL_SEC));
	return 0;
}

SYS_INIT(heap_monitor_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
