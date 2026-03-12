#include "feishu.h"
#include "feishu_config.h"
#include "feishu_client.h"
#include "feishu_ws.h"
#include "feishu_send.h"
#include "feishu_message.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "feishu";

static bool s_feishu_running = false;
static TaskHandle_t s_outbound_task_handle = NULL;

static void (*s_message_callback)(const char *chat_id, const char *content) = NULL;

void feishu_set_receive_callback(void (*callback)(const char *chat_id, const char *content))
{
    s_message_callback = callback;
}

void feishu_on_message(const char *chat_id, const char *content)
{
    ESP_LOGI(TAG, "Received message from %s: %s", chat_id, content);

    if (s_message_callback) {
        s_message_callback(chat_id, content);
    }

    mimi_msg_t msg = {0};
    strncpy(msg.channel, MIMI_CHAN_FEISHU, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    msg.content = strdup(content);
    message_bus_push_inbound(&msg);
}

bool feishu_should_process_message(const feishu_message_t *fs_msg)
{
    if (!fs_msg) {
        return false;
    }

    if (fs_msg->chat_type == FEISHU_CHAT_TYPE_P2P) {
        return true;
    }

    if (fs_msg->chat_type == FEISHU_CHAT_TYPE_GROUP) {
        if (fs_msg->mentioned_bot) {
            ESP_LOGI(TAG, "Group message mentions bot, will process");
            return true;
        } else {
            ESP_LOGI(TAG, "Group message does not mention bot, skip");
            return false;
        }
    }

    ESP_LOGW(TAG, "Unknown chat type, skip");
    return false;
}

void feishu_on_message_ex(const feishu_message_t *fs_msg)
{
    if (!fs_msg || !fs_msg->content || fs_msg->content[0] == '\0') {
        ESP_LOGW(TAG, "Empty message, skip");
        return;
    }

    ESP_LOGI(TAG, "Received message from %s: %s (msg_id=%s, sender=%s)",
             fs_msg->chat_id, fs_msg->content, fs_msg->msg_id, fs_msg->sender_id);

    if (!feishu_should_process_message(fs_msg)) {
        return;
    }

    if (s_message_callback) {
        s_message_callback(fs_msg->chat_id, fs_msg->content);
    }

    mimi_msg_t msg = {0};
    strncpy(msg.channel, MIMI_CHAN_FEISHU, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, fs_msg->chat_id, sizeof(msg.chat_id) - 1);
    strncpy(msg.message_id, fs_msg->msg_id, sizeof(msg.message_id) - 1);
    strncpy(msg.sender_id, fs_msg->sender_id, sizeof(msg.sender_id) - 1);
    strncpy(msg.parent_id, fs_msg->parent_id, sizeof(msg.parent_id) - 1);
    msg.content = strdup(fs_msg->content);
    msg.media_path[0] = '\0';

    message_bus_push_inbound(&msg);
}

static void feishu_outbound_task(void *arg)
{
    (void)arg;

    while (s_feishu_running) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, 1000) == ESP_OK) {
            if (msg.content) {
                feishu_send(msg.chat_id, msg.content);
                free(msg.content);
            }
        }
    }

    vTaskDelete(NULL);
}

esp_err_t feishu_init(void)
{
    ESP_LOGI(TAG, "Initializing Feishu...");

    esp_err_t err = feishu_config_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init config");
        return err;
    }

    if (feishu_config_is_configured() == false) {
        ESP_LOGW(TAG, "Feishu not configured. Use CLI to set app_id and app_secret.");
        return ESP_OK;
    }

    err = feishu_client_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init client");
        return err;
    }

    err = feishu_ws_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WebSocket");
        return err;
    }

    char app_name[128] = {0};
    if (feishu_client_get_app_info(app_name, sizeof(app_name)) == ESP_OK) {
        ESP_LOGI(TAG, "Feishu bot: %s", app_name);
    }

    ESP_LOGI(TAG, "Feishu initialized");
    return ESP_OK;
}

esp_err_t feishu_start(void)
{
    if (s_feishu_running) {
        ESP_LOGW(TAG, "Feishu already running");
        return ESP_OK;
    }

    if (feishu_config_is_configured() == false) {
        ESP_LOGE(TAG, "Feishu not configured");
        return ESP_FAIL;
    }

    esp_err_t err = feishu_ws_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket");
        return err;
    }

    s_feishu_running = true;

    xTaskCreatePinnedToCore(
        feishu_outbound_task,
        "feishu_outbound",
        4096,
        NULL,
        5,
        &s_outbound_task_handle,
        0
    );

    ESP_LOGI(TAG, "Feishu started");
    return ESP_OK;
}

esp_err_t feishu_stop(void)
{
    s_feishu_running = false;

    feishu_ws_stop();

    if (s_outbound_task_handle) {
        vTaskDelete(s_outbound_task_handle);
        s_outbound_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Feishu stopped");
    return ESP_OK;
}

bool feishu_is_running(void)
{
    return s_feishu_running;
}

bool feishu_is_configured(void)
{
    return feishu_config_is_configured();
}

esp_err_t feishu_send(const char *chat_id, const char *text)
{
    if (!chat_id || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    return feishu_send_text(chat_id, text);
}
