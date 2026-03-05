#pragma once

#include "feishu_types.h"
#include "esp_err.h"
#include <stddef.h>

esp_err_t feishu_message_parse(const char *json, feishu_message_t *msg);
esp_err_t feishu_message_get_text_content(const char *json, char *content, size_t len);
bool feishu_message_is_mention_bot(const char *json);
