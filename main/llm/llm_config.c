#include "llm_config.h"
#include "llm_proxy.h"
#include "mimi_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "llm_config";

/* Global model registry */
llm_model_config_t g_llm_models[LLM_MAX_MODELS];
int g_llm_model_count = 0;
int g_llm_current_model_index = 0;

/* Default model configurations */
static const llm_model_config_t s_default_models[] = {
    {
        .name = "claude-opus-4-5",
        .provider = "anthropic",
        .base_url = "",
        .api_key = "",
        .supports_tools = true,
        .supports_vision = true,
        .max_tokens = 4096,
        .priority = 1
    },
    {
        .name = "gpt-4-turbo",
        .provider = "openai",
        .base_url = "",
        .api_key = "",
        .supports_tools = true,
        .supports_vision = true,
        .max_tokens = 4096,
        .priority = 2
    },
    {
        .name = "minimax",
        .provider = "minimax",
        .base_url = "https://api.minimax.chat/v1/chat/completions",
        .api_key = "",
        .supports_tools = true,
        .supports_vision = false,
        .max_tokens = 4096,
        .priority = 3
    },
    {
        .name = "qwen-plus",
        .provider = "qwen",
        .base_url = "https://api-inference.modelscope.cn/v1/chat/completions",
        .api_key = "",
        .supports_tools = true,
        .supports_vision = false,
        .max_tokens = 4096,
        .priority = 4
    },
    {
        .name = "moonshot-v1",
        .provider = "moonshot",
        .base_url = "https://api.moonshot.cn/v1/chat/completions",
        .api_key = "",
        .supports_tools = true,
        .supports_vision = false,
        .max_tokens = 4096,
        .priority = 5
    },
    {
        .name = "glm-4-plus",
        .provider = "glm",
        .base_url = "https://open.bigmodel.cn/api/paas/v4/chat/completions",
        .api_key = "",
        .supports_tools = true,
        .supports_vision = false,
        .max_tokens = 4096,
        .priority = 6
    }
};

esp_err_t llm_config_init(void)
{
    /* Reset registry */
    g_llm_model_count = 0;
    g_llm_current_model_index = 0;

    /* Register default models */
    for (size_t i = 0; i < sizeof(s_default_models) / sizeof(s_default_models[0]); i++) {
        esp_err_t err = llm_register_model(&s_default_models[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register default model %s: %s",
                     s_default_models[i].name, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Initialized with %d models", g_llm_model_count);
    return ESP_OK;
}

esp_err_t llm_register_model(const llm_model_config_t *model)
{
    if (!model) {
        ESP_LOGE(TAG, "Cannot register NULL model");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_llm_model_count >= LLM_MAX_MODELS) {
        ESP_LOGE(TAG, "Model registry full (%d models)", LLM_MAX_MODELS);
        return ESP_ERR_NO_MEM;
    }

    /* Check for duplicate model name */
    for (int i = 0; i < g_llm_model_count; i++) {
        if (strcmp(g_llm_models[i].name, model->name) == 0) {
            ESP_LOGW(TAG, "Model %s already registered, updating", model->name);
            /* Update existing model */
            g_llm_models[i] = *model;
            return ESP_OK;
        }
    }

    /* Add new model */
    g_llm_models[g_llm_model_count] = *model;
    g_llm_model_count++;

    ESP_LOGI(TAG, "Registered model: %s (provider: %s, priority: %d)",
             model->name, model->provider, model->priority);

    return ESP_OK;
}

esp_err_t llm_switch_model(const char *model_name)
{
    if (!model_name) {
        ESP_LOGE(TAG, "Cannot switch to NULL model name");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < g_llm_model_count; i++) {
        if (strcmp(g_llm_models[i].name, model_name) == 0) {
            g_llm_current_model_index = i;
            const llm_model_config_t *model = &g_llm_models[i];

            ESP_LOGI(TAG, "Switched to model: %s (provider: %s, base_url: %s)",
                     model->name, model->provider,
                     model->base_url[0] ? model->base_url : "default");

            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Model not found: %s", model_name);
    return ESP_ERR_NOT_FOUND;
}

const llm_model_config_t *llm_get_current_model(void)
{
    if (g_llm_model_count == 0) {
        return NULL;
    }
    return &g_llm_models[g_llm_current_model_index];
}

const llm_model_config_t *llm_get_model(const char *model_name)
{
    if (!model_name) {
        return NULL;
    }

    for (int i = 0; i < g_llm_model_count; i++) {
        if (strcmp(g_llm_models[i].name, model_name) == 0) {
            return &g_llm_models[i];
        }
    }

    return NULL;
}

int llm_get_model_count(void)
{
    return g_llm_model_count;
}

esp_err_t llm_list_models(char *out_buf, size_t buf_size)
{
    if (!out_buf || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int offset = 0;
    int remaining = buf_size - 1; /* Reserve space for null terminator */

    offset += snprintf(out_buf + offset, remaining,
                      "Registered Models (%d):\n", g_llm_model_count);
    remaining -= offset;

    for (int i = 0; i < g_llm_model_count && remaining > 0; i++) {
        const llm_model_config_t *model = &g_llm_models[i];
        const char *marker = (i == g_llm_current_model_index) ? " [CURRENT]" : "";

        int written = snprintf(out_buf + offset, remaining,
                               "%2d. %s%s\n"
                               "    Provider: %s\n"
                               "    Priority: %d\n"
                               "    Tools: %s, Vision: %s\n\n",
                               i + 1, model->name, marker,
                               model->provider,
                               model->priority,
                               model->supports_tools ? "yes" : "no",
                               model->supports_vision ? "yes" : "no");

        if (written < 0 || written >= remaining) {
            /* Buffer too small */
            out_buf[buf_size - 1] = '\0';
            return ESP_ERR_INVALID_ARG;
        }

        offset += written;
        remaining -= written;
    }

    out_buf[offset] = '\0';
    return ESP_OK;
}
