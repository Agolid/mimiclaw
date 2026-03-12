#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize memory statistics module.
 */
esp_err_t memory_stats_init(void);

/**
 * Log current memory usage statistics.
 *
 * @param label Optional label for the log entry (e.g., "after_llm_call")
 */
void memory_stats_log(const char *label);

/**
 * Check if free memory is below warning threshold.
 *
 * @return true if memory is critically low, false otherwise
 */
bool memory_stats_is_critical(void);

/**
 * Get free heap size in bytes.
 */
size_t memory_stats_get_free_heap(void);

/**
 * Get largest free block in bytes.
 */
size_t memory_stats_get_largest_free_block(void);
