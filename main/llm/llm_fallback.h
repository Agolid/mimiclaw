#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#include "llm_proxy.h"
#include "cJSON.h"

/**
 * Chat with LLM with automatic fallback to other models
 *
 * This function implements a robust fallback strategy:
 * 1. Try the current model
 * 2. On permanent error, switch to the next priority model
 * 3. On temporary error, retry the same model
 * 4. Continue until success or all models exhausted
 *
 * @param system_prompt System prompt string
 * @param messages cJSON array of messages (caller owns)
 * @param tools_json Pre-built JSON string of tools array, or NULL
 * @param resp Output: structured response with text and tool calls
 * @param max_attempts_per_model Maximum attempts per model (default: 2)
 * @return ESP_OK on success, ESP_FAIL if all models failed
 */
esp_err_t llm_chat_with_fallback(const char *system_prompt,
                                  cJSON *messages,
                                  const char *tools_json,
                                  llm_response_t *resp,
                                  int max_attempts_per_model);

/**
 * Chat with LLM and return HTTP status code
 *
 * Wrapper around llm_chat_tools that also returns the HTTP status code
 * for error classification in fallback logic.
 *
 * @param system_prompt System prompt string
 * @param messages cJSON array of messages (caller owns)
 * @param tools_json Pre-built JSON string of tools array, or NULL
 * @param resp Output: structured response with text and tool calls
 * @param out_http_status Output: HTTP status code (0 if unknown)
 * @return ESP_OK on success
 */
esp_err_t llm_chat_with_status(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                llm_response_t *resp,
                                int *out_http_status);
