#include "feishu_message.h"
#include "feishu_config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "feishu_message";

static void safe_copy(char *dest, size_t dest_size, const char *src)
{
    if (!dest || !src) return;
    size_t len = strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

esp_err_t feishu_message_parse(const char *json, feishu_message_t *msg)
{
    if (!json || !msg) return ESP_ERR_INVALID_ARG;

    memset(msg, 0, sizeof(feishu_message_t));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON *schema = cJSON_GetObjectItem(root, "schema");
    if (!schema) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *msg_type = cJSON_GetObjectItem(schema, "message_type");
    if (msg_type && cJSON_IsString(msg_type)) {
        safe_copy(msg->msg_type, sizeof(msg->msg_type), msg_type->valuestring);

        if (strcmp(msg_type->valuestring, "text") == 0) {
            msg->msg_type_enum = FEISHU_MSG_TYPE_TEXT;
        } else if (strcmp(msg_type->valuestring, "image") == 0) {
            msg->msg_type_enum = FEISHU_MSG_TYPE_IMAGE;
        } else if (strcmp(msg_type->valuestring, "file") == 0) {
            msg->msg_type_enum = FEISHU_MSG_TYPE_FILE;
        } else if (strcmp(msg_type->valuestring, "interactive") == 0) {
            msg->msg_type_enum = FEISHU_MSG_TYPE_CARD;
        } else {
            msg->msg_type_enum = FEISHU_MSG_TYPE_UNKNOWN;
        }
    }

    cJSON *header = cJSON_GetObjectItem(schema, "header");
    if (header) {
        cJSON *message_id = cJSON_GetObjectItem(header, "message_id");
        if (message_id && cJSON_IsString(message_id)) {
            safe_copy(msg->msg_id, sizeof(msg->msg_id), message_id->valuestring);
        }

        cJSON *chat_id = cJSON_GetObjectItem(header, "chat_id");
        if (chat_id && cJSON_IsString(chat_id)) {
            safe_copy(msg->chat_id, sizeof(msg->chat_id), chat_id->valuestring);
        }

        cJSON *root_id = cJSON_GetObjectItem(header, "root_id");
        if (root_id && cJSON_IsString(root_id)) {
            safe_copy(msg->root_id, sizeof(msg->root_id), root_id->valuestring);
        }

        cJSON *parent_id = cJSON_GetObjectItem(header, "parent_id");
        if (parent_id && cJSON_IsString(parent_id)) {
            safe_copy(msg->parent_id, sizeof(msg->parent_id), parent_id->valuestring);
        }
    }

    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (event) {
        cJSON *sender = cJSON_GetObjectItem(event, "sender");
        if (sender) {
            cJSON *sender_id = cJSON_GetObjectItem(sender, "sender_id");
            if (sender_id) {
                cJSON *open_id = cJSON_GetObjectItem(sender_id, "open_id");
                if (open_id && cJSON_IsString(open_id)) {
                    safe_copy(msg->sender_id, sizeof(msg->sender_id), open_id->valuestring);
                }
            }
        }

        cJSON *message = cJSON_GetObjectItem(event, "message");
        if (message) {
            cJSON *body = cJSON_GetObjectItem(message, "body");
            if (body) {
                cJSON *content = cJSON_GetObjectItem(body, "content");
                if (content && cJSON_IsString(content)) {
                    safe_copy(msg->content, sizeof(msg->content), content->valuestring);
                }
            }

            cJSON *chat_id = cJSON_GetObjectItem(message, "chat_id");
            if (chat_id && cJSON_IsString(chat_id)) {
                if (msg->chat_id[0] == '\0') {
                    safe_copy(msg->chat_id, sizeof(msg->chat_id), chat_id->valuestring);
                }
            }

            cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
            if (message_id && cJSON_IsString(message_id)) {
                if (msg->msg_id[0] == '\0') {
                    safe_copy(msg->msg_id, sizeof(msg->msg_id), message_id->valuestring);
                }
            }
        }

        cJSON *chat_id = cJSON_GetObjectItem(event, "chat_id");
        if (chat_id && cJSON_IsString(chat_id)) {
            if (msg->chat_id[0] == '\0') {
                safe_copy(msg->chat_id, sizeof(msg->chat_id), chat_id->valuestring);
            }
            if (strncmp(chat_id->valuestring, "oc_", 3) == 0) {
                msg->chat_type = FEISHU_CHAT_TYPE_P2P;
            } else {
                msg->chat_type = FEISHU_CHAT_TYPE_GROUP;
            }
        }
    }

    if (msg->content[0] != '\0' && msg->msg_type_enum == FEISHU_MSG_TYPE_TEXT) {
        cJSON *content_obj = cJSON_Parse(msg->content);
        if (content_obj) {
            cJSON *text = cJSON_GetObjectItem(content_obj, "text");
            if (text && cJSON_IsString(text)) {
                safe_copy(msg->content, sizeof(msg->content), text->valuestring);
            }
            cJSON_Delete(content_obj);
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Parsed message: msg_id=%s, chat_id=%s, type=%s",
             msg->msg_id, msg->chat_id, msg->msg_type);

    return ESP_OK;
}

esp_err_t feishu_message_get_text_content(const char *json, char *content, size_t len)
{
    if (!json || !content) return ESP_ERR_INVALID_ARG;

    content[0] = '\0';

    cJSON *root = cJSON_Parse(json);
    if (!root) return ESP_FAIL;

    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (!event) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (!message) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *body = cJSON_GetObjectItem(message, "body");
    if (!body) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *msg_content = cJSON_GetObjectItem(body, "content");
    if (!msg_content || !cJSON_IsString(msg_content)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *content_obj = cJSON_Parse(msg_content->valuestring);
    if (content_obj) {
        cJSON *text = cJSON_GetObjectItem(content_obj, "text");
        if (text && cJSON_IsString(text)) {
            strncpy(content, text->valuestring, len - 1);
            content[len - 1] = '\0';
        }
        cJSON_Delete(content_obj);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

bool feishu_message_is_mention_bot(const char *json)
{
    if (!json) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    bool mentioned = false;

    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (event) {
        cJSON *message = cJSON_GetObjectItem(event, "message");
        if (message) {
            cJSON *mentions = cJSON_GetObjectItem(message, "mentions");
            if (mentions && cJSON_IsArray(mentions)) {
                int size = cJSON_GetArraySize(mentions);
                for (int i = 0; i < size; i++) {
                    cJSON *item = cJSON_GetArrayItem(mentions, i);
                    if (item) {
                        cJSON *key = cJSON_GetObjectItem(item, "key");
                        if (key && cJSON_IsString(key)) {
                            mentioned = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return mentioned;
}
