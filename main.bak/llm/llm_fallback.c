#include "llm_fallback.h"
#include "llm_config.h"
#include "llm_proxy.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "llm_fallback";

/**
 * Fallback state structure
 */
typedef struct {
    int start_index;      /* Starting model index */
    int current_index;    /* Current model being tried */
    int attempts;         /* Number of attempts made */
    int max_attempts;     /* Maximum attempts per model */
    bool success;         /* Whether fallback succeeded */
} llm_fallback_state_t;

/**
 * Check if an error is permanent (should switch models) or temporary (should retry)
 */
static bool is_permanent_error(esp_err_t err, int http_status)
{
    /* HTTP status codes that indicate permanent errors */
    if (http_status >= 400 && http_status < 500) {
        /* 4xx errors (except 429 rate limit) are usually permanent */
        if (http_status == 429) {
            return false;  /* Rate limit, retry */
        }
        return true;
    }

    /* 5xx errors are temporary server-side issues */
    if (http_status >= 500 && http_status < 600) {
        return false;
    }

    /* ESP error codes */
    switch (err) {
        case ESP_ERR_NO_MEM:
        case ESP_ERR_NOT_FOUND:
        case ESP_ERR_INVALID_ARG:
            return true;  /* Permanent */
        case ESP_ERR_TIMEOUT:
        case ESP_ERR_HTTP_CONNECT:
        case ESP_ERR_HTTP_WRITE_DATA:
        case ESP_ERR_HTTP_READ_DATA:
        case ESP_ERR_HTTP_EAGAIN:
            return false;  /* Temporary network errors */
        default:
            return true;  /* Unknown, assume permanent */
    }
}

/**
 * Log the fallback attempt
 */
static void log_fallback_attempt(const llm_fallback_state_t *state,
                                const llm_model_config_t *model,
                                esp_err_t err,
                                int http_status)
{
    if (state->current_index == state->start_index) {
        ESP_LOGI(TAG, "Fallback attempt %d/%d: trying model %s (priority %d)",
                 state->attempts + 1, state->max_attempts,
                 model->name, model->priority);
    } else {
        ESP_LOGW(TAG, "Fallback attempt %d/%d: switching to model %s (priority %d)",
                 state->attempts + 1, state->max_attempts,
                 model->name, model->priority);
    }

    if (http_status > 0) {
        ESP_LOGW(TAG, "  HTTP status: %d, ESP error: %s",
                 http_status, esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "  ESP error: %s", esp_err_to_name(err));
    }
}

esp_err_t llm_chat_with_fallback(const char *system_prompt,
                                  cJSON *messages,
                                  const char *tools_json,
                                  llm_response_t *resp,
                                  int max_attempts_per_model)
{
    if (!system_prompt || !messages || !resp) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    int start_index = g_llm_current_model_index;
    int model_count = g_llm_model_count;

    if (model_count == 0) {
        ESP_LOGE(TAG, "No models registered");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting fallback with %d models, %d attempts per model",
             model_count, max_attempts_per_model);

    llm_fallback_state_t state = {
        .start_index = start_index,
        .current_index = start_index,
        .attempts = 0,
        .max_attempts = max_attempts_per_model,
        .success = false
    };

    /* Try each model in priority order */
    for (int i = 0; i < model_count; i++) {
        int model_idx = (start_index + i) % model_count;
        state.current_index = model_idx;

        const llm_model_config_t *model = &g_llm_models[model_idx];
        if (!model || model->name[0] == '\0') {
            continue;
        }

        /* Switch to this model */
        esp_err_t switch_err = llm_switch_model(model->name);
        if (switch_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to switch to model %s: %s",
                     model->name, esp_err_to_name(switch_err));
            continue;
        }

        /* Try the model up to max_attempts_per_model times */
        state.attempts = 0;
        while (state.attempts < state.max_attempts) {
            state.attempts++;

            /* Initialize response */
            memset(resp, 0, sizeof(*resp));

            /* Call LLM API */
            int http_status = 0;
            esp_err_t err = llm_chat_with_status(system_prompt, messages,
                                                tools_json, resp,
                                                &http_status);

            if (err == ESP_OK) {
                /* Success! */
                state.success = true;
                ESP_LOGI(TAG, "Fallback succeeded with model %s after %d attempt(s)",
                         model->name, state.attempts);
                return ESP_OK;
            }

            /* Log the attempt */
            log_fallback_attempt(&state, model, err, http_status);

            /* Check if error is permanent */
            if (is_permanent_error(err, http_status)) {
                ESP_LOGW(TAG, "  Permanent error, switching to next model");
                break;  /* Try next model */
            }

            /* Temporary error, retry same model */
            if (state.attempts < state.max_attempts) {
                int delay_ms = 1000 * state.attempts;  /* Exponential backoff */
                ESP_LOGI(TAG, "  Temporary error, retrying in %d ms", delay_ms);
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
        }

        /* If we get here, this model failed all attempts */
        ESP_LOGW(TAG, "Model %s failed after %d attempt(s), trying next model",
                 model->name, state.max_attempts);
    }

    /* All models failed */
    ESP_LOGE(TAG, "Fallback failed: all %d models exhausted", model_count);

    /* Restore original model */
    llm_switch_model(g_llm_models[start_index].name);

    return ESP_FAIL;
}

esp_err_t llm_chat_with_status(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                llm_response_t *resp,
                                int *out_http_status)
{
    /* This is a wrapper around llm_chat_tools that also returns HTTP status */
    /* For now, we call llm_chat_tools and assume 200 if successful */

    esp_err_t err = llm_chat_tools(system_prompt, messages, tools_json, resp);
    if (out_http_status) {
        *out_http_status = (err == ESP_OK) ? 200 : 0;
    }
    return err;
}
