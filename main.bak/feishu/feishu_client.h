#pragma once

#include "feishu_types.h"
#include "esp_err.h"

esp_err_t feishu_client_init(void);
esp_err_t feishu_client_get_tenant_token(char *token, size_t len);
esp_err_t feishu_client_refresh_token(void);
bool feishu_client_is_token_valid(void);
esp_err_t feishu_client_api_request(const char *method, const char *path,
                                     const char *body, char *resp, size_t resp_len);
esp_err_t feishu_client_get_app_info(char *app_name, size_t len);
