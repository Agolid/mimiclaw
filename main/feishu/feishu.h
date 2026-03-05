#pragma once

#include "esp_err.h"

esp_err_t feishu_init(void);
esp_err_t feishu_start(void);
esp_err_t feishu_stop(void);
bool feishu_is_running(void);
bool feishu_is_configured(void);
esp_err_t feishu_send(const char *chat_id, const char *text);
void feishu_set_receive_callback(void (*callback)(const char *chat_id, const char *content));
