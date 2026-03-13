#pragma once

#include "feishu_types.h"
#include "esp_err.h"

esp_err_t feishu_ws_init(void);
esp_err_t feishu_ws_start(void);
esp_err_t feishu_ws_stop(void);
bool feishu_ws_is_connected(void);
void feishu_ws_task(void *arg);
