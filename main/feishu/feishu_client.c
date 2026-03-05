#include "feishu_client.h"
#include "feishu_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "feishu_client";

#define FEISHU_TOKEN_EXPIRE_BUFFER_SEC  300

static char s_token[FEISHU_TOKEN_MAX_LEN] = {0};
static int64_t s_token_expires_at = 0;

static char s_base_url[64] = {0};

static void update_base_url(void)
{
    const char *domain = feishu_config_get_domain();
    if (strcmp(domain, "lark") == 0) {
        snprintf(s_base_url, sizeof(s_base_url), "https://open.larksuite.com");
    } else {
        snprintf(s_base_url, sizeof(s_base_url), "https://open.feishu.com");
    }
}

esp_err_t feishu_client_init(void)
{
    update_base_url();
    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->user_data && evt->data_len > 0) {
            char **buf = (char **)evt->user_data;
            size_t len = strlen(*buf);
            if (len + evt->data_len < 16384) {
                memcpy(*buf + len, evt->data, evt->data_len);
                (*buf)[len + evt->data_len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t feishu_client_get_tenant_token(char *token, size_t len)
{
    if (feishu_config_is_configured() == false) {
        ESP_LOGE(TAG, "Feishu not configured");
        return ESP_FAIL;
    }

    if (s_token[0] != '\0' && s_token_expires_at > 0) {
        int64_t now = esp_timer_get_time() / 1000000;
        if (s_token_expires_at - now > FEISHU_TOKEN_EXPIRE_BUFFER_SEC) {
            strncpy(token, s_token, len - 1);
            token[len - 1] = '\0';
            return ESP_OK;
        }
    }

    const char *app_id = feishu_config_get_app_id();
    const char *app_secret = feishu_config_get_app_secret();

    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "{\"app_id\":\"%s\",\"app_secret\":\"%s\"}",
             app_id, app_secret);

    char *resp_buf = calloc(1, 16384);
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/open-apis/auth/v3/tenant_access_token/internal", s_base_url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_buf,
        .timeout_ms = 30000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            cJSON *root = cJSON_Parse(resp_buf);
            if (root) {
                cJSON *tenant_token = cJSON_GetObjectItem(root, "tenant_access_token");
                cJSON *expire = cJSON_GetObjectItem(root, "expire");
                if (tenant_token && cJSON_IsString(tenant_token)) {
                    strncpy(s_token, tenant_token->valuestring, sizeof(s_token) - 1);
                    if (expire && cJSON_IsNumber(expire)) {
                        int64_t now = esp_timer_get_time() / 1000000;
                        s_token_expires_at = now + expire->valueint - FEISHU_TOKEN_EXPIRE_BUFFER_SEC;
                    }
                    strncpy(token, s_token, len - 1);
                    token[len - 1] = '\0';
                    feishu_config_set_token(s_token, s_token_expires_at);
                    ESP_LOGI(TAG, "Got tenant_access_token");
                    cJSON_Delete(root);
                    free(resp_buf);
                    esp_http_client_cleanup(client);
                    return ESP_OK;
                }
                cJSON_Delete(root);
            }
        }
        ESP_LOGE(TAG, "HTTP status = %d, response: %s", status, resp_buf);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    free(resp_buf);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

esp_err_t feishu_client_refresh_token(void)
{
    s_token[0] = '\0';
    s_token_expires_at = 0;
    return feishu_client_get_tenant_token(s_token, sizeof(s_token));
}

bool feishu_client_is_token_valid(void)
{
    if (s_token[0] == '\0') return false;
    int64_t now = esp_timer_get_time() / 1000000;
    return (s_token_expires_at - now) > FEISHU_TOKEN_EXPIRE_BUFFER_SEC;
}

esp_err_t feishu_client_api_request(const char *method, const char *path,
                                     const char *body, char *resp, size_t resp_len)
{
    if (!path || !resp) return ESP_ERR_INVALID_ARG;

    if (feishu_client_is_token_valid() == false) {
        esp_err_t err = feishu_client_get_tenant_token(s_token, sizeof(s_token));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get token");
            return err;
        }
    }

    char url[512];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    char *resp_buf = calloc(1, resp_len > 0 ? resp_len : 16384);
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = strcmp(method, "GET") == 0 ? HTTP_METHOD_GET : HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_buf,
        .timeout_ms = 60000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }

    char auth_header[FEISHU_TOKEN_MAX_LEN + 32];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");

    if (body && strlen(body) > 0) {
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 || status == 201) {
            if (resp && resp_len > 0) {
                strncpy(resp, resp_buf, resp_len - 1);
                resp[resp_len - 1] = '\0';
            }
            free(resp_buf);
            esp_http_client_cleanup(client);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "API request failed: %d, %s", status, resp_buf);
        }
    } else {
        ESP_LOGE(TAG, "API request error: %s", esp_err_to_name(err));
    }

    free(resp_buf);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

esp_err_t feishu_client_get_app_info(char *app_name, size_t len)
{
    char resp[4096] = {0};
    esp_err_t err = feishu_client_api_request("GET", "/open-apis/bot/v3/info", NULL, resp, sizeof(resp));
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data) {
        cJSON *bot = cJSON_GetObjectItem(data, "bot");
        if (bot) {
            cJSON *name = cJSON_GetObjectItem(bot, "app_name");
            if (name && cJSON_IsString(name)) {
                strncpy(app_name, name->valuestring, len - 1);
                app_name[len - 1] = '\0';
                cJSON_Delete(root);
                return ESP_OK;
            }
        }
    }

    cJSON_Delete(root);
    return ESP_FAIL;
}
