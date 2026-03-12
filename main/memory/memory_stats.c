#include "memory_stats.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>

static const char *TAG = "memory_stats";

#define MEM_CRITICAL_THRESHOLD_INTERNAL  (20 * 1024)   /* 20 KB internal RAM */
#define MEM_CRITICAL_THRESHOLD_SPIRAM    (512 * 1024)   /* 512 KB PSRAM */

void memory_stats_log(const char *label)
{
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t total_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    if (label) {
        ESP_LOGI(TAG, "[Memory] %s:", label);
    } else {
        ESP_LOGI(TAG, "[Memory] Usage:");
    }

    ESP_LOGI(TAG, "  Internal: free=%u bytes, largest=%u bytes%s",
             (unsigned)free_internal, (unsigned)largest_internal,
             free_internal < MEM_CRITICAL_THRESHOLD_INTERNAL ? " [CRITICAL]" : "");

    if (free_psram > 0) {
        ESP_LOGI(TAG, "  PSRAM:    free=%u bytes, largest=%u bytes%s",
                 (unsigned)free_psram, (unsigned)largest_psram,
                 free_psram < MEM_CRITICAL_THRESHOLD_SPIRAM ? " [CRITICAL]" : "");
    }

    ESP_LOGI(TAG, "  Total:    free=%u bytes", (unsigned)total_free);
}

bool memory_stats_is_critical(void)
{
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    return (free_internal < MEM_CRITICAL_THRESHOLD_INTERNAL) ||
           (free_psram > 0 && free_psram < MEM_CRITICAL_THRESHOLD_SPIRAM);
}

size_t memory_stats_get_free_heap(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
}

size_t memory_stats_get_largest_free_block(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
}

esp_err_t memory_stats_init(void)
{
    ESP_LOGI(TAG, "Memory statistics module initialized");
    memory_stats_log("Startup");
    return ESP_OK;
}
