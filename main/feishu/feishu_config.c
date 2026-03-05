#include "feishu_config.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "feishu_config";

static feishu_config_t s_config = {0};

static void safe_copy(char *dest, size_t dest_size, const char *src)
{
    if (!dest || !src) return;
    size_t len = strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

esp_err_t feishu_config_init(void)
{
    memset(&s_config, 0, sizeof(s_config));

    if (MIMI_SECRET_FEISHU_APP_ID[0] != '\0') {
        safe_copy(s_config.app_id, sizeof(s_config.app_id), MIMI_SECRET_FEISHU_APP_ID);
    }
    if (MIMI_SECRET_FEISHU_APP_SECRET[0] != '\0') {
        safe_copy(s_config.app_secret, sizeof(s_config.app_secret), MIMI_SECRET_FEISHU_APP_SECRET);
    }
    if (MIMI_SECRET_FEISHU_DOMAIN[0] != '\0') {
        safe_copy(s_config.domain, sizeof(s_config.domain), MIMI_SECRET_FEISHU_DOMAIN);
    } else {
        safe_copy(s_config.domain, sizeof(s_config.domain), "feishu");
    }

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEISHU, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[FEISHU_APP_ID_MAX_LEN] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_FEISHU_APP_ID, tmp, &len) == ESP_OK && tmp[0]) {
            safe_copy(s_config.app_id, sizeof(s_config.app_id), tmp);
        }
        char secret_tmp[FEISHU_APP_SECRET_MAX_LEN] = {0};
        len = sizeof(secret_tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_FEISHU_SECRET, secret_tmp, &len) == ESP_OK && secret_tmp[0]) {
            safe_copy(s_config.app_secret, sizeof(s_config.app_secret), secret_tmp);
        }
        char domain_tmp[FEISHU_DOMAIN_MAX_LEN] = {0};
        len = sizeof(domain_tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_FEISHU_DOMAIN, domain_tmp, &len) == ESP_OK && domain_tmp[0]) {
            safe_copy(s_config.domain, sizeof(s_config.domain), domain_tmp);
        }
        nvs_close(nvs);
    }

    s_config.initialized = true;
    ESP_LOGI(TAG, "Feishu config initialized (app_id: %s, domain: %s)",
             s_config.app_id[0] ? s_config.app_id : "(not set)",
             s_config.domain);

    return ESP_OK;
}

esp_err_t feishu_config_set_app_id(const char *app_id)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_FEISHU_APP_ID, app_id));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    safe_copy(s_config.app_id, sizeof(s_config.app_id), app_id);
    ESP_LOGI(TAG, "Feishu app_id set to: %s", app_id);
    return ESP_OK;
}

esp_err_t feishu_config_set_app_secret(const char *app_secret)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_FEISHU_SECRET, app_secret));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    safe_copy(s_config.app_secret, sizeof(s_config.app_secret), app_secret);
    ESP_LOGI(TAG, "Feishu app_secret set");
    return ESP_OK;
}

esp_err_t feishu_config_set_domain(const char *domain)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_FEISHU_DOMAIN, domain));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    safe_copy(s_config.domain, sizeof(s_config.domain), domain);
    ESP_LOGI(TAG, "Feishu domain set to: %s", domain);
    return ESP_OK;
}

esp_err_t feishu_config_get(feishu_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memcpy(config, &s_config, sizeof(feishu_config_t));
    return ESP_OK;
}

bool feishu_config_is_configured(void)
{
    return s_config.app_id[0] != '\0' && s_config.app_secret[0] != '\0';
}

esp_err_t feishu_config_set_token(const char *token, uint64_t expires_at)
{
    safe_copy(s_config.tenant_access_token, sizeof(s_config.tenant_access_token), token);
    s_config.token_expires_at = expires_at;
    return ESP_OK;
}

const char* feishu_config_get_app_id(void)
{
    return s_config.app_id[0] ? s_config.app_id : NULL;
}

const char* feishu_config_get_domain(void)
{
    return s_config.domain[0] ? s_config.domain : "feishu";
}
