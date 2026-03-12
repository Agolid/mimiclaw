#include "retry_utils.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "retry_utils";

bool error_is_retryable(esp_err_t err)
{
    /* Retryable errors: network, timeout, no memory */
    switch (err) {
        case ESP_ERR_TIMEOUT:
        case ESP_ERR_HTTP_CONNECT:
        case ESP_ERR_HTTP_CONNECTION_REFUSED:
        case ESP_ERR_HTTP_WRITE_BLOCKED:
        case ESP_ERR_HTTP_READ_BLOCKED:
        case ESP_ERR_HTTP_MAX_REDIRECT:
        case ESP_ERR_NOT_FINISHED:
        case ESP_ERR_NO_MEM:
            return true;

        default:
            return false;
    }
}

bool http_status_is_retryable(int status)
{
    /* 429 Too Many Requests - retry with backoff */
    if (status == 429) {
        return true;
    }

    /* 5xx Server errors - retry */
    if (status >= 500 && status < 600) {
        return true;
    }

    /* 408 Request Timeout - retry */
    if (status == 408) {
        return true;
    }

    /* Other 4xx errors - do not retry */
    return false;
}

static int calculate_delay_ms(const retry_config_t *config, int retry_count)
{
    int delay = config->base_delay_ms;

    /* Exponential backoff */
    for (int i = 0; i < retry_count; i++) {
        delay *= 2;
        if (delay > config->max_delay_ms) {
            delay = config->max_delay_ms;
            break;
        }
    }

    /* Add jitter if enabled */
    if (config->use_jitter) {
        /* Add +/- 25% jitter */
        int jitter = delay / 4;
        int random_offset = esp_random() % (2 * jitter + 1);
        delay = delay - jitter + random_offset;
    }

    return delay;
}

esp_err_t retry_with_backoff(const retry_config_t *config,
                             esp_err_t (*func)(void *arg),
                             void *arg,
                             int *out_retry)
{
    if (!config || !func) {
        return ESP_ERR_INVALID_ARG;
    }

    int retry_count = 0;
    esp_err_t err;

    while (retry_count <= config->max_retries) {
        err = func(arg);

        if (err == ESP_OK) {
            if (out_retry) {
                *out_retry = retry_count;
            }
            return ESP_OK;
        }

        retry_count++;

        /* Check if we should retry */
        if (retry_count > config->max_retries) {
            break;
        }

        if (!error_is_retryable(err)) {
            ESP_LOGW(TAG, "Error not retryable: %s", esp_err_to_name(err));
            break;
        }

        /* Calculate delay and wait */
        int delay_ms = calculate_delay_ms(config, retry_count);
        ESP_LOGW(TAG, "Retry %d/%d after %d ms (error: %s)",
                 retry_count, config->max_retries, delay_ms, esp_err_to_name(err));

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    ESP_LOGE(TAG, "All %d retries failed: %s",
             config->max_retries + 1, esp_err_to_name(err));

    if (out_retry) {
        *out_retry = retry_count;
    }

    return err;
}

void error_log_detail(const char *tag, const char *func, int line,
                      esp_err_t err, const char *context)
{
    if (!tag || !func) {
        return;
    }

    ESP_LOGE(tag, "%s:%d: %s (err=%d, desc=%s)%s%s",
             func, line,
             context ? context : "",
             (int)err, esp_err_to_name(err),
             context ? ": " : "",
             context ? context : "");
}
