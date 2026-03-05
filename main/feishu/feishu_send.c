#include "feishu_send.h"
#include "feishu_client.h"
#include "feishu_config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "feishu_send";

esp_err_t feishu_send_text(const char *receive_id, const char *text)
{
    if (!receive_id || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "receive_id_type", "open_id");
    cJSON_AddStringToObject(root, "receive_id", receive_id);

    cJSON *msg_content = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_content, "text", text);
    cJSON_AddItemToObject(root, "content", msg_content);

    char *post_data = cJSON_PrintUnformatted(root);
    if (!post_data) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char path[256];
    snprintf(path, sizeof(path), "/open-apis/im/v1/messages?receive_id_type=open_id");

    char resp[4096] = {0};
    esp_err_t err = feishu_client_api_request("POST", path, post_data, resp, sizeof(resp));

    free(post_data);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message: %s", resp);
        return err;
    }

    ESP_LOGI(TAG, "Message sent to %s", receive_id);
    return ESP_OK;
}

esp_err_t feishu_send_card(const char *receive_id, const char *card_json)
{
    if (!receive_id || !card_json) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "receive_id_type", "open_id");
    cJSON_AddStringToObject(root, "receive_id", receive_id);

    cJSON *msg_content = cJSON_CreateObject();
    cJSON_AddRawToObject(msg_content, "card", card_json);
    cJSON_AddItemToObject(root, "content", msg_content);

    char *post_data = cJSON_PrintUnformatted(root);
    if (!post_data) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char path[256];
    snprintf(path, sizeof(path), "/open-apis/im/v1/messages?receive_id_type=open_id");

    char resp[4096] = {0};
    esp_err_t err = feishu_client_api_request("POST", path, post_data, resp, sizeof(resp));

    free(post_data);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send card: %s", resp);
        return err;
    }

    ESP_LOGI(TAG, "Card sent to %s", receive_id);
    return ESP_OK;
}

esp_err_t feishu_reply_message(const char *message_id, const char *text)
{
    if (!message_id || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msg_type", "text");
    
    cJSON *msg_content = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_content, "text", text);
    cJSON_AddItemToObject(root, "content", msg_content);

    char *post_data = cJSON_PrintUnformatted(root);
    if (!post_data) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char path[256];
    snprintf(path, sizeof(path), "/open-apis/im/v1/messages/%s/reply", message_id);

    char resp[4096] = {0};
    esp_err_t err = feishu_client_api_request("POST", path, post_data, resp, sizeof(resp));

    free(post_data);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reply message: %s", resp);
        return err;
    }

    ESP_LOGI(TAG, "Reply sent to message %s", message_id);
    return ESP_OK;
}
