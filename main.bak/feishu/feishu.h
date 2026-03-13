#pragma once

#include "esp_err.h"
#include "feishu_message.h"

esp_err_t feishu_init(void);
esp_err_t feishu_start(void);
esp_err_t feishu_stop(void);
bool feishu_is_running(void);
bool feishu_is_configured(void);
esp_err_t feishu_send(const char *chat_id, const char *text);
void feishu_set_receive_callback(void (*callback)(const char *chat_id, const char *content));
bool feishu_should_process_message(const feishu_message_t *fs_msg);
void feishu_on_message_ex(const feishu_message_t *fs_msg);
