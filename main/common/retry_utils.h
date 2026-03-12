#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Retry context structure for network operations.
 */
typedef struct {
    int max_retries;         /* Maximum number of retries */
    int base_delay_ms;       /* Base delay for exponential backoff (ms) */
    int max_delay_ms;        /* Maximum delay for exponential backoff (ms) */
    bool use_jitter;         /* Add random jitter to delay */
} retry_config_t;

/**
 * Default retry configuration.
 */
#define RETRY_CONFIG_DEFAULT() \
    { \
        .max_retries = 3, \
        .base_delay_ms = 1000, \
        .max_delay_ms = 10000, \
        .use_jitter = true \
    }

/**
 * Retry a function with exponential backoff.
 *
 * @param config     Retry configuration
 * @param func       Function to retry (signature: esp_err_t func(void *arg))
 * @param arg        Argument passed to function
 * @param out_retry  Output: number of retries performed (can be NULL)
 * @return ESP_OK on success, error code if all retries fail
 */
esp_err_t retry_with_backoff(const retry_config_t *config,
                             esp_err_t (*func)(void *arg),
                             void *arg,
                             int *out_retry);

/**
 * Check if an error code should trigger a retry.
 *
 * @param err Error code to check
 * @return true if error is retryable, false otherwise
 */
bool error_is_retryable(esp_err_t err);

/**
 * Helper function to check if HTTP status code should trigger a retry.
 *
 * @param status HTTP status code
 * @return true if status is retryable, false otherwise
 */
bool http_status_is_retryable(int status);

/**
 * Detailed error logging with context.
 *
 * @param tag     Log tag
 * @param func    Function name
 * @param line    Line number
 * @param err     Error code
 * @param context Optional context string (can be NULL)
 */
void error_log_detail(const char *tag, const char *func, int line,
                      esp_err_t err, const char *context);

#define ERROR_LOG_DETAIL(tag, err, context) \
    error_log_detail(tag, __func__, __LINE__, err, context)
