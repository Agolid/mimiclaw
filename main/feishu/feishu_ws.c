#include "feishu_ws.h"
#include "feishu_config.h"
#include "feishu_client.h"
#include "feishu_message.h"
#include "feishu_send.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "feishu_ws";

#define FEISHU_WS_STACK_SIZE   (8 * 1024)
#define FEISHU_WS_PRIO         5
#define FEISHU_WS_RECONNECT_DELAY_MS  5000

static bool s_ws_connected = false;
static bool s_ws_running = false;
static esp_websocket_client_handle_t s_ws_client = NULL;
static TaskHandle_t s_ws_task_handle = NULL;

extern void feishu_on_message(const char *chat_id, const char *content);

static char* feishu_ws_get_wss_url(void)
{
    static char url[512] = {0};
    const char *domain = feishu_config_get_domain();
    const char *app_id = feishu_config_get_app_id();

    char token[FEISHU_TOKEN_MAX_LEN];
    if (feishu_client_get_tenant_token(token, sizeof(token)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get token for WS URL");
        return NULL;
    }

    if (strcmp(domain, "lark") == 0) {
        snprintf(url, sizeof(url),
                 "wss://ws-rpc.%s/open-apis/im/v1/messages?token=%s&receive_id_type=open_id",
                 domain, token);
    } else {
        snprintf(url, sizeof(url),
                 "wss://wsrpc.%s/open-apis/im/v1/messages?token=%s&receive_id_type=open_id",
                 domain, token);
    }

    return url;
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        s_ws_connected = true;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WebSocket disconnected");
        s_ws_connected = false;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_len > 0) {
            char *msg = (char *)malloc(data->data_len + 1);
            if (msg) {
                memcpy(msg, data->data_ptr, data->data_len);
                msg[data->data_len] = '\0';

                ESP_LOGI(TAG, "WS received: %.*s", data->data_len > 100 ? 100 : (int)data->data_len, msg);

                cJSON *root = cJSON_Parse(msg);
                if (root) {
                    cJSON *type = cJSON_GetObjectItem(root, "type");
                    if (type && cJSON_IsString(type)) {
                        if (strcmp(type->valuestring, "ping") == 0) {
                            esp_websocket_client_send_text(s_ws_client, "{\"type\":\"pong\"}", -1);
                        } else if (strcmp(type->valuestring, "pong") == 0) {
                            ESP_LOGD(TAG, "Received pong");
                        } else if (strcmp(type->valuestring, "message") == 0) {
                            cJSON *body = cJSON_GetObjectItem(root, "body");
                            if (body) {
                                char *body_str = cJSON_PrintUnformatted(body);
                                if (body_str) {
                                    char content[4096] = {0};
                                    feishu_message_get_text_content(body_str, content, sizeof(content));
                                    if (content[0] != '\0') {
                                        cJSON *header = cJSON_GetObjectItem(body, "header");
                                        if (header) {
                                            cJSON *chat_id_item = cJSON_GetObjectItem(header, "chat_id");
                                            if (chat_id_item && cJSON_IsString(chat_id_item)) {
                                                feishu_on_message(chat_id_item->valuestring, content);
                                            }
                                        }
                                    }
                                    free(body_str);
                                }
                            }
                        }
                    }
                    cJSON_Delete(root);
                }
                free(msg);
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

esp_err_t feishu_ws_init(void)
{
    return ESP_OK;
}

esp_err_t feishu_ws_start(void)
{
    if (s_ws_running) {
        ESP_LOGW(TAG, "WebSocket already running");
        return ESP_OK;
    }

    if (feishu_config_is_configured() == false) {
        ESP_LOGE(TAG, "Feishu not configured");
        return ESP_FAIL;
    }

    s_ws_running = true;

    xTaskCreatePinnedToCore(
        feishu_ws_task,
        "feishu_ws",
        FEISHU_WS_STACK_SIZE,
        NULL,
        FEISHU_WS_PRIO,
        &s_ws_task_handle,
        0
    );

    ESP_LOGI(TAG, "WebSocket task started");
    return ESP_OK;
}

esp_err_t feishu_ws_stop(void)
{
    s_ws_running = false;

    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    if (s_ws_task_handle) {
        vTaskDelete(s_ws_task_handle);
        s_ws_task_handle = NULL;
    }

    s_ws_connected = false;
    ESP_LOGI(TAG, "WebSocket stopped");
    return ESP_OK;
}

bool feishu_ws_is_connected(void)
{
    return s_ws_connected;
}

void feishu_ws_task(void *arg)
{
    (void)arg;

    while (s_ws_running) {
        if (!s_ws_connected) {
            char *url = feishu_ws_get_wss_url();
            if (!url) {
                ESP_LOGE(TAG, "Failed to get WS URL, retrying...");
                vTaskDelay(pdMS_TO_TICKS(FEISHU_WS_RECONNECT_DELAY_MS));
                continue;
            }

            esp_websocket_client_config_t config = {
                .uri = url,
                .task_prio = FEISHU_WS_PRIO,
                .task_stack = FEISHU_WS_STACK_SIZE,
                .disable_auto_reconnect = false,
                .reconnect_timeout_ms = FEISHU_WS_RECONNECT_DELAY_MS,
            };

            s_ws_client = esp_websocket_client_init(&config);
            if (!s_ws_client) {
                ESP_LOGE(TAG, "Failed to init WebSocket client");
                vTaskDelay(pdMS_TO_TICKS(FEISHU_WS_RECONNECT_DELAY_MS));
                continue;
            }

            esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);

            esp_err_t err = esp_websocket_client_start(s_ws_client);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
                esp_websocket_client_destroy(s_ws_client);
                s_ws_client = NULL;
                vTaskDelay(pdMS_TO_TICKS(FEISHU_WS_RECONNECT_DELAY_MS));
                continue;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}
