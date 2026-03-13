#pragma once

#include "feishu_types.h"
#include "esp_err.h"

esp_err_t feishu_config_init(void);
esp_err_t feishu_config_set_app_id(const char *app_id);
esp_err_t feishu_config_set_app_secret(const char *app_secret);
esp_err_t feishu_config_set_domain(const char *domain);
esp_err_t feishu_config_get(feishu_config_t *config);
bool feishu_config_is_configured(void);
esp_err_t feishu_config_set_token(const char *token, uint64_t expires_at);
const char* feishu_config_get_app_id(void);
const char* feishu_config_get_domain(void);
esp_err_t feishu_config_set_allowed_users(const char *users_json);
esp_err_t feishu_config_set_allowed_groups(const char *groups_json);
bool feishu_config_is_user_allowed(const char *user_id);
bool feishu_config_is_group_allowed(const char *group_id);
const char* feishu_config_get_allowed_users(void);
const char* feishu_config_get_allowed_groups(void);
