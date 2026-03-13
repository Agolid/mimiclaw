#include "feishu_polling.h"
#include "feishu_config.h"
#include "feishu_client.h"
#include "feishu_message.h"
#include "feishu_types.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "feishu_polling";

#define FEISHU_POLL_STACK_SIZE   (8 * 1024)
#define FEISHU_POLL_PRIO         5
#define FEISHU_POLL_INTERVAL_MS  30000  // 30秒轮询一次

// HTTP响应缓冲区
#define HTTP_RESPONSE_BUF_SIZE   8192

static bool s_polling_running = false;
static bool s_connected = false;
static TaskHandle_t s_poll_task_handle = NULL;
static esp_http_client_handle_t s_http_client = NULL;

// 飞书API endpoint
static const char *FEISHU_API_BASE = "https://open.feishu.cn/open-apis/im/v1/messages";

// 响应缓冲区
static char s_http_response[HTTP_RESPONSE_BUF_SIZE] = {0};
static int s_http_response_len = 0;

/**
 * @brief HTTP数据处理器 - 累积数据
 */
static esp_err_t feishu_polling_http_data_handler(esp_http_client_event_t *evt)
{
    int copy_len = evt->data_len;
    if (s_http_response_len + copy_len < HTTP_RESPONSE_BUF_SIZE - 1) {
        memcpy(s_http_response + s_http_response_len, evt->data, copy_len);
        s_http_response_len += copy_len;
    } else {
        ESP_LOGW(TAG, "HTTP response buffer overflow, dropping data");
    }
    return ESP_OK;
}

/**
 * @brief 构建带认证的飞书API URL
 */
static char* feishu_polling_build_url(void)
{
    static char url[512] = {0};
    const char *domain = feishu_config_get_domain();

    if (strcmp(domain, "lark") == 0) {
        snprintf(url, sizeof(url),
                 "%s?user_id_type=open_id&container_id_type=open_id&limit=20",
                 "https://open.feishu.cn/open-apis/im/v1/messages");
    } else {
        snprintf(url, sizeof(url),
                 "%s?user_id_type=open_id&container_id_type=open_id&limit=20",
                 "https://open.larksuite.com/open-apis/im/v1/messages");
    }

    return url;
}

/**
 * @brief HTTP事件处理器
 */
static esp_err_t feishu_polling_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        return feishu_polling_http_data_handler(evt);

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP finished, total length: %d", s_http_response_len);
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP disconnected");
        break;

    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP error: %d", evt->error_handle);
        break;

    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief 解析并处理飞书消息
 */
static void feishu_polling_process_response(void)
{
    if (s_http_response_len == 0) {
        ESP_LOGW(TAG, "Empty response");
        return;
    }

    s_http_response[s_http_response_len] = '\0';
    ESP_LOGI(TAG, "Polling response: %.*s", s_http_response_len > 200 ? 200 : s_http_response_len, s_http_response);

    // 解析JSON响应
    cJSON *root = cJSON_Parse(s_http_response);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsArray(data)) {
        int msg_count = cJSON_GetArraySize(data);
        ESP_LOGI(TAG, "Got %d messages", msg_count);

        // 从后往前处理（最新的在前）
        for (int i = msg_count - 1; i >= 0; i--) {
            cJSON *item = cJSON_GetArrayItem(data, i);
            cJSON *body = cJSON_GetObjectItem(item, "body");
            if (cJSON_IsString(body)) {
                char *body_str = cJSON_PrintUnformatted(body);
                if (body_str) {
                    feishu_message_t fs_msg;
                    if (feishu_message_parse(body_str, &fs_msg) == ESP_OK) {
                        if (fs_msg.content[0] != '\0') {
                            // 调用消息处理回调
                            extern void feishu_on_message_ex(const feishu_message_t *msg);
                            feishu_on_message_ex(&fs_msg);
                        }
                    }
                    free(body_str);
                }
            }
        }

        s_connected = true;
    }

    cJSON_Delete(root);
}

/**
 * @brief 执行HTTP轮询请求
 */
static esp_err_t feishu_polling_request(void)
{
    char *url = feishu_polling_build_url();
    if (!url) {
        return ESP_FAIL;
    }

    char token[FEISHU_TOKEN_MAX_LEN];
    if (feishu_client_get_tenant_token(token, sizeof(token)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get tenant token");
        return ESP_FAIL;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);

    // 清空响应缓冲区
    s_http_response_len = 0;

    // 配置HTTP客户端
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = feishu_polling_http_event_handler,
        .timeout_ms = 15000,  // 增加超时时间
        .disable_auto_redirect = true,
    };

    s_http_client = esp_http_client_init(&config);
    if (!s_http_client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    // 设置认证头
    esp_http_client_set_header(s_http_client, "Authorization", auth_header);
    esp_http_client_set_header(s_http_client, "Content-Type", "application/json");

    // 执行请求
    esp_err_t err = esp_http_client_perform(s_http_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        return err;
    }

    // 处理响应
    feishu_polling_process_response();

    esp_http_client_cleanup(s_http_client);
    s_http_client = NULL;

    return ESP_OK;
}

/**
 * @brief 轮询任务主循环
 */
static void feishu_polling_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Polling task started");

    while (s_polling_running) {
        // 执行轮询请求
        esp_err_t err = feishu_polling_request();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Polling request failed: %s, will retry in 30s", esp_err_to_name(err));
            s_connected = false;
        }

        // 等待30秒再下一次轮询
        for (int i = 0; i < FEISHU_POLL_INTERVAL_MS / 1000 && s_polling_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "Polling task stopped");
    vTaskDelete(NULL);
}

// ========== 公开API实现 ==========

esp_err_t feishu_polling_init(void)
{
    ESP_LOGI(TAG, "Initializing Feishu HTTP polling");
    return ESP_OK;
}

esp_err_t feishu_polling_start(void)
{
    if (s_polling_running) {
        ESP_LOGW(TAG, "Polling already running");
        return ESP_OK;
    }

    if (feishu_config_is_configured() == false) {
        ESP_LOGE(TAG, "Feishu not configured");
        return ESP_FAIL;
    }

    s_polling_running = true;
    s_connected = false;

    xTaskCreatePinnedToCore(
        feishu_polling_task,
        "feishu_poll",
        FEISHU_POLL_STACK_SIZE,
        NULL,
        FEISHU_POLL_PRIO,
        &s_poll_task_handle,
        0
    );

    ESP_LOGI(TAG, "Polling task created");
    return ESP_OK;
}

esp_err_t feishu_polling_stop(void)
{
    s_polling_running = false;

    if (s_http_client) {
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
    }

    if (s_poll_task_handle) {
        vTaskDelete(s_poll_task_handle);
        s_poll_task_handle = NULL;
    }

    s_connected = false;
    ESP_LOGI(TAG, "Polling stopped");
    return ESP_OK;
}

bool feishu_polling_is_running(void)
{
    return s_polling_running;
}

bool feishu_polling_is_connected(void)
{
    return s_connected;
}
