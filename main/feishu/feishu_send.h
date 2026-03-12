#pragma once

#include "feishu_types.h"
#include "esp_err.h"

esp_err_t feishu_send_text(const char *receive_id, const char *text);
esp_err_t feishu_send_markdown(const char *receive_id, const char *markdown);
esp_err_t feishu_send_card(const char *receive_id, const char *card_json);
esp_err_t feishu_reply_message(const char *message_id, const char *text);
