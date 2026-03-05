#pragma once

#include <stddef.h>
#include <stdbool.h>

#define FEISHU_APP_ID_MAX_LEN     64
#define FEISHU_APP_SECRET_MAX_LEN  128
#define FEISHU_DOMAIN_MAX_LEN     32
#define FEISHU_TOKEN_MAX_LEN      256
#define FEISHU_MSG_ID_MAX_LEN     64
#define FEISHU_CHAT_ID_MAX_LEN    64
#define FEISHU_USER_ID_MAX_LEN    64

typedef enum {
    FEISHU_CHAT_TYPE_P2P,
    FEISHU_CHAT_TYPE_GROUP,
    FEISHU_CHAT_TYPE_UNKNOWN
} feishu_chat_type_t;

typedef enum {
    FEISHU_MSG_TYPE_TEXT,
    FEISHU_MSG_TYPE_IMAGE,
    FEISHU_MSG_TYPE_FILE,
    FEISHU_MSG_TYPE_CARD,
    FEISHU_MSG_TYPE_UNKNOWN
} feishu_msg_type_t;

typedef struct {
    char msg_id[FEISHU_MSG_ID_MAX_LEN];
    char chat_id[FEISHU_CHAT_ID_MAX_LEN];
    char sender_id[FEISHU_USER_ID_MAX_LEN];
    char content[4096];
    char msg_type[32];
    feishu_chat_type_t chat_type;
    feishu_msg_type_t msg_type_enum;
    bool mentioned_bot;
    char root_id[FEISHU_MSG_ID_MAX_LEN];
    char parent_id[FEISHU_MSG_ID_MAX_LEN];
} feishu_message_t;

typedef struct {
    char app_id[FEISHU_APP_ID_MAX_LEN];
    char app_secret[FEISHU_APP_SECRET_MAX_LEN];
    char domain[FEISHU_DOMAIN_MAX_LEN];
    char tenant_access_token[FEISHU_TOKEN_MAX_LEN];
    uint64_t token_expires_at;
    bool initialized;
    bool running;
} feishu_config_t;

typedef enum {
    FEISHU_OK = 0,
    FEISHU_ERR_INVALID_PARAM,
    FEISHU_ERR_NOT_CONFIGURED,
    FEISHU_ERR_TOKEN_FAILED,
    FEISHU_ERR_HTTP_FAILED,
    FEISHU_ERR_WS_CONNECT_FAILED,
    FEISHU_ERR_PARSE_FAILED,
    FEISHU_ERR_NO_MEMORY
} feishu_err_t;
