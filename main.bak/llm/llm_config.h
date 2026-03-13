#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * Model configuration structure
 * Stores metadata for each registered LLM model
 */
typedef struct {
    char name[64];           /* Model name (e.g., "claude-3-opus", "gpt-4-turbo") */
    char provider[16];       /* Provider type: "anthropic", "openai", "minimax", "qwen", "moonshot", "glm" */
    char base_url[256];      /* API Base URL (optional, can use default) */
    char api_key[128];       /* API Key (optional, can use global config) */
    bool supports_tools;     /* Whether the model supports tool/function calling */
    bool supports_vision;    /* Whether the model supports vision/image input */
    int max_tokens;         /* Maximum token count for responses */
    int priority;            /* Priority for automatic fallback (lower = higher priority) */
} llm_model_config_t;

/**
 * Model registry configuration
 */
#define LLM_MAX_MODELS 8

/* Static model registry */
extern llm_model_config_t g_llm_models[LLM_MAX_MODELS];
extern int g_llm_model_count;
extern int g_llm_current_model_index;

/**
 * Initialize the LLM model configuration system
 * Loads default models and registers them
 */
esp_err_t llm_config_init(void);

/**
 * Register a new model configuration
 *
 * @param model Pointer to model configuration structure
 * @return ESP_OK on success, ESP_ERR_NO_MEM if registry full, ESP_ERR_INVALID_ARG if NULL
 */
esp_err_t llm_register_model(const llm_model_config_t *model);

/**
 * Switch to a different model by name
 *
 * @param model_name Name of the model to switch to
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if model not found
 */
esp_err_t llm_switch_model(const char *model_name);

/**
 * Get the current active model configuration
 *
 * @return Pointer to current model config, or NULL if no models registered
 */
const llm_model_config_t *llm_get_current_model(void);

/**
 * Get a model configuration by name
 *
 * @param model_name Name of the model to find
 * @return Pointer to model config, or NULL if not found
 */
const llm_model_config_t *llm_get_model(const char *model_name);

/**
 * Get total number of registered models
 *
 * @return Number of registered models
 */
int llm_get_model_count(void);

/**
 * List all registered models
 *
 * @param out_buf Output buffer for formatted model list
 * @param buf_size Size of output buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if buffer too small
 */
esp_err_t llm_list_models(char *out_buf, size_t buf_size);
