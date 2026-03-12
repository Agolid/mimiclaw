# MimiClaw Telegram 实现分析报告

> **分析时间**: 2026-03-13
> **分析对象**: MimiClaw ESP32-S3 AI Agent - Telegram Bot 实现
> **工作目录**: `/home/gem/workspace/agent/workspace/mimiclaw`

---

## 目录

1. [项目架构概述](#1-项目架构概述)
2. [Telegram 实现关键代码分析](#2-telegram-实现关键代码分析)
3. [消息处理流程图](#3-消息处理流程图)
4. [LLM API 调用流程](#4-llm-api-调用流程)
5. [适合接入飞书的位置和接口](#5-适合接入飞书的位置和接口)
6. [适合添加多模型支持的位置](#6-适合添加多模型支持的位置)

---

## 1. 项目架构概述

### 1.1 系统总览

MimiClaw 是一个基于 ESP32-S3 的嵌入式 AI 代理系统，采用 C 语言和 FreeRTOS 实现。系统架构为**消息总线驱动的多任务架构**，核心组件包括：

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3 (MimiClaw)                       │
│                                                               │
│  ┌─────────────┐    ┌──────────────┐    ┌──────────────┐    │
│  │  Telegram   │───▶│  Inbound     │───▶│  Agent Loop  │    │
│  │  Poller     │    │  Queue       │    │  (Core 1)    │    │
│  │  (Core 0)   │    │              │    │              │    │
│  └─────────────┘    └──────────────┘    └──────┬───────┘    │
│                                                │            │
│  ┌─────────────┐    ┌──────────────┐         ▼            │
│  │  Feishu     │───▶│              │    ┌──────────────┐  │
│  │  WebSocket  │    │              │    │  LLM Proxy   │  │
│  │  (Core 0)   │    │              │    │  (HTTPS)     │  │
│  └─────────────┘    └──────────────┘    └──────┬───────┘  │
│                                                │            │
│  ┌─────────────┐                               │            │
│  │  Serial CLI │                               ▼            │
│  │  (Core 0)   │                        ┌──────────────┐  │
│  └─────────────┘                        │  Outbound    │  │
│                                        │  Queue       │  │
│  ┌─────────────┐                      └──────┬───────┘  │
│  │  WebSocket  │                              │            │
│  │  Server     │                              ▼            │
│  │  (18789)    │                       ┌──────────────┐  │
│  └─────────────┘                       │  Dispatch    │  │
│                                        │  (Core 0)    │  │
│                                        └──────┬───────┘  │
│                                               │            │
│                                        ┌──────▼───────┐  │
│                                        │   Channels   │  │
│                                        │ (TG/FS/WS)   │  │
│                                        └──────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 核心模块

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **消息总线** | `bus/message_bus.c` | FreeRTOS 双队列（入队/出队）实现消息路由 |
| **Telegram Bot** | `telegram/telegram_bot.c` | Telegram API 集成（Long Polling + 消息发送） |
| **Feishu Client** | `feishu/*.c` | 飞书 API 集成（WebSocket + HTTP） |
| **Agent Loop** | `agent/agent_loop.c` | ReAct 循环实现，LLM 调用与工具执行 |
| **LLM Proxy** | `llm/llm_proxy.c` | LLM API 调用层（Anthropic/OpenAI 格式） |
| **工具注册** | `tools/tool_registry.c` | 工具注册与分发系统 |
| **内存管理** | `memory/session_mgr.c` | 会话管理与持久化（JSONL + SPIFFS） |
| **HTTP 代理** | `proxy/http_proxy.c` | HTTP CONNECT 隧道支持 |

### 1.3 FreeRTOS 任务布局

| 任务名称 | 核心号 | 优先级 | 栈大小 | 描述 |
|----------|--------|--------|--------|------|
| `tg_poll` | 0 | 5 | 12 KB | Telegram Long Polling（30s 超时） |
| `agent_loop` | 1 | 6 | 24 KB | 消息处理 + LLM API 调用 |
| `outbound_dispatch` | 0 | 5 | 12 KB | 路由响应到各通道 |
| `serial_cli` | 0 | 3 | 4 KB | 串口控制台 REPL |
| `feishu_outbound` | 0 | 5 | 4 KB | Feishu 出队分发任务 |
| `feishu_ws_task` | 0 | 5 | 8 KB | Feishu WebSocket 接收任务 |
| httpd (内部) | 0 | 5 | — | WebSocket 服务器（esp_http_server） |

**核心分配策略**：Core 0 处理 I/O（网络、串口、WiFi）；Core 1 专用于 Agent Loop（CPU 密集型 JSON 构建 + HTTPS 等待）。

---

## 2. Telegram 实现关键代码分析

### 2.1 核心文件结构

```
main/telegram/
├── telegram_bot.h    # 公共 API 声明
└── telegram_bot.c     # Telegram Bot 实现代码（约 600 行）
```

### 2.2 关键数据结构

#### 2.2.1 消息总线消息类型

```c
typedef struct {
    char channel[16];   /* "telegram", "websocket", "cli", "feishu" */
    char chat_id[32];   /* Telegram chat ID 或 WS 客户端 ID */
    char *content;      /* 堆分配的消息文本（调用者必须释放） */
} mimi_msg_t;
```

**设计要点**：
- `content` 采用堆内存，所有权在 `push` 时转移，接收方必须 `free()`
- 支持多通道路由（Telegram/Feishu/WebSocket/CLI）
- `chat_id` 作为会话标识，用于会话历史管理

#### 2.2.2 Telegram 内部状态

```c
static char s_bot_token[128] = MIMI_SECRET_TG_TOKEN;  // Bot Token
static int64_t s_update_offset = 0;                    // 更新偏移量
static int64_t s_last_saved_offset = -1;               // 最后保存的偏移量
static int64_t s_last_offset_save_us = 0;               // 最后保存时间戳

#define TG_DEDUP_CACHE_SIZE     64                     // 去重缓存大小
static uint64_t s_seen_msg_keys[TG_DEDUP_CACHE_SIZE];  // 消息键缓存
static size_t s_seen_msg_idx = 0;                      // 缓存索引
```

**设计要点**：
- `update_offset` 实现消息持久化，避免重启后处理重复消息
- 去重缓存使用环形缓冲区，防止相同 `update_id` 重复处理
- Token 支持编译时配置（`mimi_secrets.h`）和运行时 NVS 覆盖

### 2.3 Long Polling 消息接收

#### 2.3.1 Polling 任务核心逻辑

```c
static void telegram_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Telegram polling task started");

    while (1) {
        // 检查 Token 配置
        if (s_bot_token[0] == '\0') {
            ESP_LOGW(TAG, "No bot token configured, waiting...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // 构造 getUpdates 请求 URL
        char params[128];
        snprintf(params, sizeof(params),
                 "getUpdates?offset=%" PRId64 "&timeout=%d",
                 s_update_offset, MIMI_TG_POLL_TIMEOUT_S);

        // 调用 Telegram API
        char *resp = tg_api_call(params, NULL);
        if (resp) {
            process_updates(resp);
            free(resp);
        } else {
            // 错误退避
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}
```

**设计要点**：
- 使用 Telegram Long Polling API（`getUpdates`），超时 30 秒
- 无 Token 时每 5 秒重试，避免 CPU 空转
- API 调用失败时 3 秒退避，防止雪崩

#### 2.3.2 消息处理与去重

```c
static void process_updates(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    // ... JSON 解析与验证

    cJSON *update;
    cJSON_ArrayForEach(update, result) {
        // 提取 update_id
        cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        int64_t uid = -1;
        if (cJSON_IsNumber(update_id)) {
            uid = (int64_t)update_id->valuedouble;
        }

        // 跳过过期/重复更新
        if (uid >= 0) {
            if (uid < s_update_offset) {
                continue;
            }
            s_update_offset = uid + 1;
            save_update_offset_if_needed(false);
        }

        // 提取消息内容
        cJSON *message = cJSON_GetObjectItem(update, "message");
        cJSON *text = cJSON_GetObjectItem(message, "text");
        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        cJSON *chat_id = cJSON_GetObjectItem(chat, "id");

        // 构造消息键并去重
        int msg_id_val = -1;
        cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
        if (cJSON_IsNumber(message_id)) {
            msg_id_val = (int)message_id->valuedouble;
        }

        char chat_id_str[32];
        // ... chat_id 转字符串

        if (msg_id_val >= 0) {
            uint64_t msg_key = make_msg_key(chat_id_str, msg_id_val);
            if (seen_msg_contains(msg_key)) {
                ESP_LOGW(TAG, "Drop duplicate message update_id=%" PRId64 " chat=%s message_id=%d",
                         uid, chat_id_str, msg_id_val);
                continue;
            }
            seen_msg_insert(msg_key);
        }

        // 推送到入队
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_TELEGRAM, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id_str, sizeof(msg.chat_id) - 1);
        msg.content = strdup(text->valuestring);
        if (msg.content) {
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                ESP_LOGW(TAG, "Inbound queue full, drop telegram message");
                free(msg.content);
            }
        }
    }
}
```

**关键机制**：

1. **消息键构造**：
```c
static uint64_t make_msg_key(const char *chat_id, int msg_id)
{
    uint64_t h = fnv1a64(chat_id);  // FNV-1a 哈希
    return (h << 16) ^ (uint64_t)(msg_id & 0xFFFF) ^ ((uint64_t)msg_id << 32);
}
```

2. **去重缓存**：使用 64 条消息的环形缓冲区，足够应对短期重复

3. **Offset 持久化**：
```c
static void save_update_offset_if_needed(bool force)
{
    // 条件 1：偏移量变化超过 10
    // 条件 2：距离上次保存超过 5 秒
    if (s_update_offset <= 0) return;

    int64_t now = esp_timer_get_time();
    bool should_save = force;
    if (!should_save && s_last_saved_offset >= 0) {
        if ((s_update_offset - s_last_saved_offset) >= TG_OFFSET_SAVE_STEP) {
            should_save = true;
        } else if ((now - s_last_offset_save_us) >= TG_OFFSET_SAVE_INTERVAL_US) {
            should_save = true;
        }
    }

    if (should_save) {
        // 保存到 NVS
        nvs_handle_t nvs;
        if (nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs) == ESP_OK) {
            if (nvs_set_i64(nvs, TG_OFFSET_NVS_KEY, s_update_offset) == ESP_OK) {
                if (nvs_commit(nvs) == ESP_OK) {
                    s_last_saved_offset = s_update_offset;
                    s_last_offset_save_us = now;
                }
            }
            nvs_close(nvs);
        }
    }
}
```

### 2.4 HTTP 请求实现

#### 2.4.1 代理路径（HTTP CONNECT 隧道）

```c
static char *tg_api_call_via_proxy(const char *path, const char *post_data)
{
    // 1. 建立到代理的 TCP 连接
    proxy_conn_t *conn = proxy_conn_open("api.telegram.org", 443,
                                          (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000);
    if (!conn) return NULL;

    // 2. 构造 HTTP CONNECT 请求
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "POST /bot%s/%s HTTP/1.1\r\n"
        "Host: api.telegram.org\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        s_bot_token, path, (int)strlen(post_data));

    // 3. 发送请求头和正文
    proxy_conn_write(conn, header, hlen);
    if (post_data) {
        proxy_conn_write(conn, post_data, strlen(post_data));
    }

    // 4. 读取完整响应
    size_t cap = 4096, len = 0;
    char *buf = calloc(1, cap);
    int timeout = (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000;
    while (1) {
        if (len + 1024 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) break;
            buf = tmp;
        }
        int n = proxy_conn_read(conn, buf + len, cap - len - 1, timeout);
        if (n <= 0) break;
        len += n;
    }
    buf[len] = '\0';
    proxy_conn_close(conn);

    // 5. 跳过 HTTP 头，返回正文
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) { free(buf); return NULL; }
    body += 4;

    char *result = strdup(body);
    free(buf);
    return result;
}
```

**设计要点**：
- 支持 HTTP CONNECT 隧道（`proxy_conn_open` 内部处理 TLS 握手）
- 动态扩容缓冲区，适应大响应
- 手动解析 HTTP 头，提取 JSON 正文

#### 2.4.2 直接路径（esp_http_client）

```c
static char *tg_api_call_direct(const char *method, const char *post_data)
{
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", s_bot_token, method);

    http_resp_t resp = {
        .buf = calloc(1, 4096),
        .len = 0,
        .cap = 4096,
    };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,  // HTTPS 证书验证
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp.buf);
        return NULL;
    }

    if (post_data) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        free(resp.buf);
        return NULL;
    }

    return resp.buf;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->len + evt->data_len >= resp->cap) {
            size_t new_cap = resp->cap * 2;
            char *tmp = realloc(resp->buf, new_cap);
            if (!tmp) return ESP_ERR_NO_MEM;
            resp->buf = tmp;
            resp->cap = new_cap;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}
```

**设计要点**：
- 使用 ESP-IDF 的 `esp_http_client` 组件，自动处理 HTTPS
- 事件驱动式数据接收，动态扩容缓冲区
- 自动证书验证（`esp_crt_bundle_attach`）

#### 2.4.3 路由选择

```c
static char *tg_api_call(const char *method, const char *post_data)
{
    if (http_proxy_is_enabled()) {
        return tg_api_call_via_proxy(method, post_data);
    }
    return tg_api_call_direct(method, post_data);
}
```

### 2.5 消息发送实现

```c
esp_err_t telegram_send_message(const char *chat_id, const char *text)
{
    if (s_bot_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send: no bot token");
        return ESP_ERR_INVALID_STATE;
    }

    // 分片发送：超过 4096 字符自动分割
    size_t text_len = strlen(text);
    size_t offset = 0;
    int all_ok = 1;

    while (offset < text_len) {
        size_t chunk = text_len - offset;
        if (chunk > MIMI_TG_MAX_MSG_LEN) {
            chunk = MIMI_TG_MAX_MSG_LEN;
        }

        // 构造 JSON 请求体
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "chat_id", chat_id);

        char *segment = malloc(chunk + 1);
        memcpy(segment, text + offset, chunk);
        segment[chunk] = '\0';

        cJSON_AddStringToObject(body, "text", segment);
        cJSON_AddStringToObject(body, "parse_mode", "Markdown");

        char *json_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        free(segment);

        // 发送 Markdown 格式
        char *resp = tg_api_call("sendMessage", json_str);
        free(json_str);

        int sent_ok = 0;
        bool markdown_failed = false;
        if (resp) {
            const char *desc = NULL;
            sent_ok = tg_response_is_ok(resp, &desc);
            if (!sent_ok) {
                markdown_failed = true;
                ESP_LOGI(TAG, "Markdown rejected by Telegram for %s: %s",
                         chat_id, desc ? desc : "unknown");
            }
        }

        // Markdown 失败，回退到纯文本
        if (!sent_ok) {
            cJSON *body2 = cJSON_CreateObject();
            cJSON_AddStringToObject(body2, "chat_id", chat_id);
            char *seg2 = malloc(chunk + 1);
            if (seg2) {
                memcpy(seg2, text + offset, chunk);
                seg2[chunk] = '\0';
                cJSON_AddStringToObject(body2, "text", seg2);
                free(seg2);
            }
            char *json2 = cJSON_PrintUnformatted(body2);
            cJSON_Delete(body2);
            if (json2) {
                char *resp2 = tg_api_call("sendMessage", json2);
                free(json2);
                if (resp2) {
                    const char *desc2 = NULL;
                    sent_ok = tg_response_is_ok(resp2, &desc2);
                    if (!sent_ok) {
                        ESP_LOGE(TAG, "Plain send failed: %s", desc2 ? desc2 : "unknown");
                    }
                    free(resp2);
                }
            }
        }

        if (!sent_ok) {
            all_ok = 0;
        } else {
            if (markdown_failed) {
                ESP_LOGI(TAG, "Plain-text fallback succeeded for %s", chat_id);
            }
            ESP_LOGI(TAG, "Telegram send success to %s (%d bytes)", chat_id, (int)chunk);
        }

        free(resp);
        offset += chunk;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}
```

**设计要点**：
- 自动分片（Telegram 消息限制 4096 字符）
- Markdown 格式优先，失败后回退到纯文本
- 错误重试机制

---

## 3. 消息处理流程图

### 3.1 完整数据流

```
┌─────────────────────────────────────────────────────────────────────┐
│                        用户发送消息到 Telegram                        │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Telegram Long Polling (getUpdates)                                   │
│  - 轮询间隔: 30 秒                                                     │
│  - 使用 update_offset 确保不丢失消息                                   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  process_updates() - 消息解析与去重                                   │
│  1. JSON 解析 (cJSON)                                                │
│  2. 检查 update_id < s_update_offset (跳过过期)                       │
│  3. 更新 s_update_offset = update_id + 1                             │
│  4. 构造消息键: make_msg_key(chat_id, message_id)                    │
│  5. 检查去重缓存: seen_msg_contains(msg_key)                         │
│  6. 插入缓存: seen_msg_insert(msg_key)                               │
│  7. 持久化 offset: save_update_offset_if_needed()                    │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  推送到入队 (message_bus_push_inbound)                                │
│  mimi_msg_t:                                                         │
│    - channel = "telegram"                                            │
│    - chat_id = "<chat_id>"                                           │
│    - content = strdup(user_message)                                  │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Inbound Queue (FreeRTOS xQueue)                                     │
│  - 队列深度: 16                                                       │
│  - 阻塞等待: message_bus_pop_inbound(UINT32_MAX)                    │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Agent Loop (Core 1) - ReAct 循环                                     │
│                                                                       │
│  1. 构建系统提示 (context_build_system_prompt)                        │
│     - SOUL.md (AI 个性)                                              │
│     - USER.md (用户配置)                                              │
│     - MEMORY.md (长期记忆)                                            │
│     - memory/YYYY-MM-DD.md (最近 3 天)                               │
│     - 工具使用指导                                                    │
│                                                                       │
│  2. 加载会话历史 (session_get_history_json)                          │
│     - 从 SPIFFS 读取 /spiffs/sessions/tg_<chat_id>.jsonl             │
│     - JSONL 格式: {"role":"user","content":"...","ts":123456}        │
│     - 环形缓冲: 最多保留 20 条消息                                    │
│                                                                       │
│  3. 构造消息数组 (cJSON messages)                                      │
│     - 历史 + 当前用户消息                                              │
│                                                                       │
│  4. ReAct 循环 (最多 10 次迭代)                                       │
│     ┌───────────────────────────────────────────────────────────┐   │
│     │ a. 调用 LLM API (llm_chat_tools)                          │   │
│     │    - system_prompt                                       │   │
│     │    - messages (历史)                                       │   │
│     │    - tools_json (工具定义)                                │   │
│     │                                                           │   │
│     │ b. 解析响应 (llm_response_t)                              │   │
│     │    - resp.text: 文本内容                                  │   │
│     │    - resp.tool_use: 是否需要工具调用                       │   │
│     │    - resp.calls[]: 工具调用列表                             │   │
│     │                                                           │   │
│     │ c. 如果 resp.tool_use == true:                             │   │
│     │    - 构造 assistant 消息 (包含 text + tool_use 块)         │   │
│     │    - 执行工具 (tool_registry_execute)                     │   │
│     │      - web_search → Brave Search API                      │   │
│     │      - 其他工具...                                         │   │
│     │    - 构造 tool_result 消息                                 │   │
│     │    - 将两条消息追加到 messages 数组                         │   │
│     │    - 继续循环                                              │   │
│     │                                                           │   │
│     │ d. 如果 resp.tool_use == false:                            │   │
│     │    - 保存 final_text                                      │   │
│     │    - 退出循环                                              │   │
│     └───────────────────────────────────────────────────────────┘   │
│                                                                       │
│  5. 保存会话 (session_append)                                         │
│     - 用户消息 → sessions/tg_<chat_id>.jsonl                         │
│     - Assistant 响应 → sessions/tg_<chat_id>.jsonl                   │
│                                                                       │
│  6. 推送到出队 (message_bus_push_outbound)                             │
│     - mimi_msg_t.channel = "telegram"                                 │
│     - mimi_msg_t.chat_id = 原始 chat_id                               │
│     - mimi_msg_t.content = final_text                                 │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Outbound Queue (FreeRTOS xQueue)                                    │
│  - 队列深度: 16                                                       │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Outbound Dispatch (Core 0) - 消息路由                                │
│                                                                       │
│  while (1) {                                                          │
│      if (message_bus_pop_outbound(&msg, UINT32_MAX) == ESP_OK) {     │
│          if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {          │
│              telegram_send_message(msg.chat_id, msg.content);         │
│          } else if (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0) {     │
│              feishu_send(msg.chat_id, msg.content);                  │
│          } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {  │
│              ws_server_send(msg.chat_id, msg.content);               │
│          }                                                           │
│          free(msg.content);                                          │
│      }                                                               │
│  }                                                                   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  telegram_send_message() - 消息发送                                  │
│  1. 检查消息长度，超过 4096 字符自动分割                              │
│  2. 构造 JSON: {chat_id, text, parse_mode: "Markdown"}              │
│  3. 发送到 Telegram API (sendMessage)                                 │
│  4. 如果 Markdown 失败，回退到纯文本                                   │
│  5. 发送所有分片                                                      │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        用户收到 Telegram 回复                          │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 任务间通信图

```
┌─────────────────────────────────────────────────────────────────────┐
│                         FreeRTOS 任务调度                              │
│                                                                       │
│  Core 0 (I/O 核心)              Core 1 (计算核心)                    │
│  ┌─────────────────┐          ┌─────────────────┐                    │
│  │  tg_poll        │          │                 │                    │
│  │  (Telegram      │          │                 │                    │
│  │   Long Poll)    │          │                 │                    │
│  └────────┬────────┘          │                 │                    │
│           │                   │                 │                    │
│           │ push_inbound()    │                 │                    │
│           ▼                   │                 │                    │
│  ┌─────────────────┐          │                 │                    │
│  │  Inbound Queue  │◀─────────┤                 │                    │
│  │  (xQueue)       │          │                 │                    │
│  └────────┬────────┘          │                 │                    │
│           │ pop_inbound()     │                 │                    │
│           │                   │                 │                    │
│           │ ───────────────────┼─────────────▶  │                    │
│           │                   │                 │                    │
│           │                   │ ┌─────────────┐ │                    │
│           │                   │ │ agent_loop  │ │                    │
│           │                   │ │ (ReAct Loop)│ │                    │
│           │                   │ └──────┬──────┘ │                    │
│           │                   │        │        │                    │
│           │                   │        │ llm_chat_tools()           │
│           │                   │        ▼        │                    │
│           │                   │ ┌─────────────┐ │                    │
│           │                   │ │  LLM Proxy  │ │                    │
│           │                   │ └──────┬──────┘ │                    │
│           │                   │        │        │                    │
│           │                   │        │ HTTPS Request             │
│           │                   │        ▼        │                    │
│           │                   │ ┌─────────────┐ │                    │
│           │                   │ │  LLM API    │ │                    │
│           │                   │ │  (Claude/   │ │                    │
│           │                   │ │   OpenAI)   │ │                    │
│           │                   │ └──────┬──────┘ │                    │
│           │                   │        │        │                    │
│           │                   │        │ tool_execution             │
│           │                   │        ▼        │                    │
│           │                   │ ┌─────────────┐ │                    │
│           │                   │ │  Tools      │ │                    │
│           │                   │ └──────┬──────┘ │                    │
│           │                   │        │        │                    │
│           │                   │        │ push_outbound()           │
│           │                   │        │        │                    │
│           │ ───────────────────┼─────────┘      │                    │
│           │                   │                 │                    │
│           ▼                   │                 │                    │
│  ┌─────────────────┐          │                 │                    │
│  │  Outbound Queue │──────────┤                 │                    │
│  │  (xQueue)       │          │                 │                    │
│  └────────┬────────┘          │                 │                    │
│           │ pop_outbound()    │                 │                    │
│           ▼                   │                 │                    │
│  ┌─────────────────┐          │                 │                    │
│  │  outbound_      │          │                 │                    │
│  │  dispatch       │          │                 │                    │
│  └────────┬────────┘          │                 │                    │
│           │                   │                 │                    │
│           │ telegram_send()   │                 │                    │
│           ▼                   │                 │                    │
│  ┌─────────────────┐          │                 │                    │
│  │  Telegram API   │          │                 │                    │
│  │  (sendMessage)   │          │                 │                    │
│  └─────────────────┘          │                 │                    │
│                                                                       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 4. LLM API 调用流程

### 4.1 LLM Proxy 模块概述

`llm/llm_proxy.c` 是 LLM API 调用层的核心实现，支持：

- **Anthropic Messages API** (默认)
- **OpenAI Chat Completions API** (兼容接口)
- **工具调用 (Tool Use)** 协议
- **非流式响应**（一次性接收完整 JSON）

### 4.2 配置与初始化

```c
static char s_api_key[LLM_API_KEY_MAX_LEN] = {0};
static char s_model[LLM_MODEL_MAX_LEN] = MIMI_LLM_DEFAULT_MODEL;
static char s_provider[16] = MIMI_LLM_PROVIDER_DEFAULT;
static char s_base_url[256] = {0};

esp_err_t llm_proxy_init(void)
{
    // 1. 从编译时配置加载默认值
    if (MIMI_SECRET_API_KEY[0] != '\0') {
        safe_copy(s_api_key, sizeof(s_api_key), MIMI_SECRET_API_KEY);
    }
    if (MIMI_SECRET_MODEL[0] != '\0') {
        safe_copy(s_model, sizeof(s_model), MIMI_SECRET_MODEL);
    }
    if (MIMI_SECRET_MODEL_PROVIDER[0] != '\0') {
        safe_copy(s_provider, sizeof(s_provider), MIMI_SECRET_MODEL_PROVIDER);
    }
    if (MIMI_SECRET_BASE_URL[0] != '\0') {
        safe_copy(s_base_url, sizeof(s_base_url), MIMI_SECRET_BASE_URL);
    }

    // 2. 从 NVS 加载运行时配置（优先级最高）
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_LLM, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[LLM_API_KEY_MAX_LEN] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_API_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            safe_copy(s_api_key, sizeof(s_api_key), tmp);
        }
        // ... 加载 model, provider, base_url
        nvs_close(nvs);
    }

    return ESP_OK;
}
```

**配置优先级**：NVS（运行时） > 编译时配置（`mimi_secrets.h`） > 默认值

### 4.3 Anthropic Messages API 调用

#### 4.3.1 请求构造

```c
esp_err_t llm_chat_tools(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp)
{
    // 1. 构造请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", s_model);
    cJSON_AddNumberToObject(body, "max_tokens", MIMI_LLM_MAX_TOKENS);

    if (provider_is_openai()) {
        // OpenAI 格式
        cJSON *openai_msgs = convert_messages_openai(system_prompt, messages);
        cJSON_AddItemToObject(body, "messages", openai_msgs);
        if (tools_json) {
            cJSON *tools = convert_tools_openai(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
                cJSON_AddStringToObject(body, "tool_choice", "auto");
            }
        }
    } else {
        // Anthropic 格式
        cJSON_AddStringToObject(body, "system", system_prompt);
        cJSON *msgs_copy = cJSON_Duplicate(messages, 1);
        cJSON_AddItemToObject(body, "messages", msgs_copy);

        if (tools_json) {
            cJSON *tools = cJSON_Parse(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
            }
        }
    }

    // 2. 序列化为 JSON
    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    // 3. 发起 HTTP 请求
    resp_buf_t rb;
    resp_buf_init(&rb, MIMI_LLM_STREAM_BUF_SIZE);

    int status = 0;
    esp_err_t err = llm_http_call(post_data, &rb, &status);
    free(post_data);

    // 4. 解析响应
    // ... (见 4.3.2)
}
```

#### 4.3.2 响应解析

```c
// Anthropic 响应示例:
// {
//   "content": [
//     {"type": "text", "text": "Let me search for that."},
//     {"type": "tool_use", "id": "toolu_xxx", "name": "web_search", "input": {"query": "weather today"}}
//   ],
//   "stop_reason": "tool_use"
// }

cJSON *content = cJSON_GetObjectItem(root, "content");
if (content && cJSON_IsArray(content)) {
    // 1. 提取所有 text 块
    size_t total_text = 0;
    cJSON *block;
    cJSON_ArrayForEach(block, content) {
        cJSON *btype = cJSON_GetObjectItem(block, "type");
        if (btype && strcmp(btype->valuestring, "text") == 0) {
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text)) {
                total_text += strlen(text->valuestring);
            }
        }
    }

    // 分配并复制文本
    if (total_text > 0) {
        resp->text = calloc(1, total_text + 1);
        if (resp->text) {
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (!btype || strcmp(btype->valuestring, "text") != 0) continue;
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (!text || !cJSON_IsString(text)) continue;
                size_t tlen = strlen(text->valuestring);
                memcpy(resp->text + resp->text_len, text->valuestring, tlen);
                resp->text_len += tlen;
            }
            resp->text[resp->text_len] = '\0';
        }
    }

    // 2. 提取 tool_use 块
    cJSON_ArrayForEach(block, content) {
        cJSON *btype = cJSON_GetObjectItem(block, "type");
        if (!btype || strcmp(btype->valuestring, "tool_use") != 0) continue;
        if (resp->call_count >= MIMI_MAX_TOOL_CALLS) break;

        llm_tool_call_t *call = &resp->calls[resp->call_count];

        cJSON *id = cJSON_GetObjectItem(block, "id");
        if (id && cJSON_IsString(id)) {
            strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
        }

        cJSON *name = cJSON_GetObjectItem(block, "name");
        if (name && cJSON_IsString(name)) {
            strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
        }

        cJSON *input = cJSON_GetObjectItem(block, "input");
        if (input) {
            char *input_str = cJSON_PrintUnformatted(input);
            if (input_str) {
                call->input = input_str;
                call->input_len = strlen(input_str);
            }
        }

        resp->call_count++;
    }
}

// 3. 检查 stop_reason
cJSON *stop_reason = cJSON_GetObjectItem(root, "stop_reason");
if (stop_reason && cJSON_IsString(stop_reason)) {
    resp->tool_use = (strcmp(stop_reason->valuestring, "tool_use") == 0);
}
```

### 4.4 HTTP 调用实现

```c
static esp_err_t llm_http_call(const char *post_data, resp_buf_t *rb, int *out_status)
{
    if (http_proxy_is_enabled()) {
        return llm_http_via_proxy(post_data, rb, out_status);
    } else {
        return llm_http_direct(post_data, rb, out_status);
    }
}
```

#### 4.4.1 直接路径（esp_http_client）

```c
static esp_err_t llm_http_direct(const char *post_data, resp_buf_t *rb, int *out_status)
{
    esp_http_client_config_t config = {
        .url = llm_api_url(),
        .event_handler = http_event_handler,
        .user_data = rb,
        .timeout_ms = 120 * 1000,  // 2 分钟超时
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (provider_is_openai()) {
        if (s_api_key[0]) {
            char auth[LLM_API_KEY_MAX_LEN + 16];
            snprintf(auth, sizeof(auth), "Bearer %s", s_api_key);
            esp_http_client_set_header(client, "Authorization", auth);
        }
    } else {
        esp_http_client_set_header(client, "x-api-key", s_api_key);
        esp_http_client_set_header(client, "anthropic-version", MIMI_LLM_API_VERSION);
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return err;
}
```

#### 4.4.2 代理路径（HTTP CONNECT 隧道）

```c
static esp_err_t llm_http_via_proxy(const char *post_data, resp_buf_t *rb, int *out_status)
{
    proxy_conn_t *conn = proxy_conn_open(llm_api_host(), 443, 30000);
    if (!conn) return ESP_ERR_HTTP_CONNECT;

    int body_len = strlen(post_data);
    char header[1024];
    int hlen = 0;

    if (provider_is_openai()) {
        hlen = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            llm_api_path(), llm_api_host(), s_api_key, body_len);
    } else {
        hlen = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "x-api-key: %s\r\n"
            "anthropic-version: %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            llm_api_path(), llm_api_host(), s_api_key, MIMI_LLM_API_VERSION, body_len);
    }

    proxy_conn_write(conn, header, hlen);
    proxy_conn_write(conn, post_data, body_len);

    // 读取完整响应
    char tmp[4096];
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 120000);
        if (n <= 0) break;
        if (resp_buf_append(rb, tmp, n) != ESP_OK) break;
    }
    proxy_conn_close(conn);

    // 解析状态码
    *out_status = 0;
    if (rb->len > 5 && strncmp(rb->data, "HTTP/", 5) == 0) {
        const char *sp = strchr(rb->data, ' ');
        if (sp) *out_status = atoi(sp + 1);
    }

    // 跳过 HTTP 头
    char *body = strstr(rb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = rb->len - (body - rb->data);
        memmove(rb->data, body, blen);
        rb->len = blen;
        rb->data[rb->len] = '\0';
    }

    // 解码 chunked 传输编码
    resp_buf_decode_chunked(rb);

    return ESP_OK;
}
```

### 4.5 工具调用循环（ReAct）

```c
// Agent Loop 中的 ReAct 循环
while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
    // 1. 调用 LLM
    llm_response_t resp;
    err = llm_chat_tools(system_prompt, messages, tools_json, &resp);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LLM call failed: %s", esp_err_to_name(err));
        break;
    }

    // 2. 如果不需要工具调用，返回文本
    if (!resp.tool_use) {
        if (resp.text && resp.text_len > 0) {
            final_text = strdup(resp.text);
        }
        llm_response_free(&resp);
        break;
    }

    // 3. 构造 assistant 消息（包含 text + tool_use 块）
    cJSON *asst_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(asst_msg, "role", "assistant");
    cJSON_AddItemToObject(asst_msg, "content", build_assistant_content(&resp));
    cJSON_AddItemToArray(messages, asst_msg);

    // 4. 执行工具并构造 tool_result 消息
    cJSON *tool_results = build_tool_results(&resp, &msg, tool_output, TOOL_OUTPUT_SIZE);
    cJSON *result_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(result_msg, "role", "user");
    cJSON_AddItemToObject(result_msg, "content", tool_results);
    cJSON_AddItemToArray(messages, result_msg);

    llm_response_free(&resp);
    iteration++;
}
```

---

## 5. 适合接入飞书的位置和接口

### 5.1 现有飞书实现分析

MimiClaw 已经实现了基础的飞书集成，位于 `main/feishu/` 目录：

```
main/feishu/
├── feishu.c              # 飞书主模块（生命周期管理）
├── feishu.h
├── feishu_client.c       # 飞书 API 客户端（HTTP 请求）
├── feishu_client.h
├── feishu_config.c       # 飞书配置（app_id, app_secret）
├── feishu_config.h
├── feishu_message.c      # 消息解析（JSON → 结构化）
├── feishu_message.h
├── feishu_send.c         # 消息发送（文本、卡片）
├── feishu_send.h
├── feishu_types.h        # 数据结构定义
├── feishu_ws.c           # WebSocket 连接
└── feishu_ws.h
```

**当前实现特点**：
- ✅ WebSocket 长连接接收消息
- ✅ HTTP API 发送消息（文本/卡片）
- ✅ 消息解析（提及机器人检测）
- ⚠️ **功能基础**：仅支持文本消息，不支持富媒体、文件、语音等
- ⚠️ **未完全集成**：消息推送到消息总线，但 Agent Loop 未全面处理

### 5.2 推荐接入点

#### 5.2.1 消息接收层（已实现，可增强）

**位置**：`main/feishu/feishu.c` → `feishu_on_message()`

**当前实现**：
```c
void feishu_on_message(const char *chat_id, const char *content)
{
    ESP_LOGI(TAG, "Received message from %s: %s", chat_id, content);

    mimi_msg_t msg = {0};
    strncpy(msg.channel, MIMI_CHAN_FEISHU, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    msg.content = strdup(content);
    message_bus_push_inbound(&msg);
}
```

**建议增强**：
1. **消息类型支持**：扩展 `feishu_message_t` 结构，支持富媒体、文件、语音
2. **@ 机器人检测**：已实现 `feishu_message_is_mention_bot()`，可用于群聊场景
3. **消息元数据**：添加 `message_id`、`parent_id`（回复）、`sender_id` 字段
4. **去重机制**：类似 Telegram 的 `update_offset` 和消息缓存

**代码示例**：
```c
// 扩展后的消息结构
typedef struct {
    char channel[16];
    char chat_id[32];
    char message_id[64];      // 飞书消息 ID
    char sender_id[64];       // 发送者 Open ID
    char parent_id[64];      // 父消息 ID（回复消息）
    char *content;
    char media_path[256];     // 媒体文件路径（图片/语音）
} mimi_msg_t;
```

#### 5.2.2 消息发送层（已实现，可增强）

**位置**：`main/feishu/feishu_send.c`

**当前实现**：
- `feishu_send_text()` - 发送纯文本
- `feishu_send_card()` - 发送卡片消息
- `feishu_reply_message()` - 回复消息

**建议增强**：
1. **Markdown 转 HTML**：类似 Telegram，实现 Markdown → 飞书富文本转换
2. **富媒体发送**：图片、文件、语音上传与发送
3. **进度反馈**：长时间任务（如文件上传）时发送"处理中"状态
4. **错误处理**：重试机制、降级策略

**代码示例**：
```c
// Markdown 转飞书富文本
esp_err_t feishu_send_markdown(const char *receive_id, const char *markdown)
{
    // 1. 解析 Markdown（粗体、斜体、代码块、链接等）
    // 2. 转换为飞书富文本格式
    // 3. 调用 feishu_send_card()
}
```

#### 5.2.3 Agent Loop 层（需适配）

**位置**：`main/agent/agent_loop.c`

**当前状态**：Agent Loop 已通过消息总线处理所有通道，**无需修改**即可支持飞书。

**建议适配**：
1. **会话隔离**：飞书会话 ID 与 Telegram 不同，确保 `chat_id` 正确传递
2. **群聊场景**：检查 `feishu_message_is_mention_bot()`，仅处理 @ 机器人的消息
3. **权限控制**：实现飞书用户白名单（类似 Telegram 的 `allow_from`）

**代码示例**：
```c
// Agent Loop 开头添加群聊检查
if (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0) {
    // 检查是否 @ 机器人（群聊场景）
    if (is_group_chat(msg.chat_id) && !msg.mentions_bot) {
        ESP_LOGI(TAG, "Feishu group message without mention, skip");
        free(msg.content);
        continue;
    }

    // 检查白名单
    if (!is_feishu_user_allowed(msg.sender_id)) {
        ESP_LOGW(TAG, "Feishu user not allowed: %s", msg.sender_id);
        free(msg.content);
        continue;
    }
}
```

#### 5.2.4 配置层（已实现，需补充）

**位置**：`main/feishu/feishu_config.c`

**当前实现**：
- `feishu_config_init()` - 初始化配置
- `feishu_config_is_configured()` - 检查是否配置

**建议补充**：
1. **用户白名单**：存储允许的飞书用户 Open ID
2. **群聊白名单**：存储允许的飞书群聊 ID
3. **机器人权限**：配置是否支持群聊 @、富媒体等功能

**代码示例**：
```c
// 飞书配置结构
typedef struct {
    bool enabled;
    char app_id[64];
    char app_secret[128];
    char domain[16];                    // "feishu" 或 "lark"
    char allowed_users[256];            // JSON 数组: ["ou_xxx", "ou_yyy"]
    char allowed_groups[256];           // JSON 数组: ["oc_xxx", "oc_yyy"]
    bool enable_group_mention;
} feishu_config_t;
```

### 5.3 飞书 vs Telegram 对照表

| 功能 | Telegram | 飞书 | 推荐实现位置 |
|------|----------|------|------------|
| **消息接收** | Long Polling (getUpdates) | WebSocket | `feishu_ws.c` |
| **消息发送** | sendMessage | sendMessage (HTTP API) | `feishu_send.c` |
| **长文本处理** | 自动分片（4096 字符） | 无限制 | 无需特殊处理 |
| **富文本格式** | Markdown（有 fallback） | 富文本/卡片 | `feishu_send.c` |
| **图片/文件** | 支持下载与发送 | 支持上传与发送 | 新增 `feishu_media.c` |
| **语音** | 支持转录 | 支持 | 新增 `feishu_voice.c` |
| **群聊场景** | 无限制 | @ 机器人 | `feishu_message.c` |
| **权限控制** | 无白名单 | 需白名单 | `feishu_config.c` |
| **消息去重** | update_offset + 缓存 | 需实现 | `feishu_ws.c` |
| **会话管理** | JSONL 文件 | JSONL 文件 | 复用 `session_mgr.c` |

### 5.4 飞书接入推荐顺序

```
1. 基础功能验证（已完成）
   ✅ WebSocket 消息接收
   ✅ HTTP 消息发送
   ✅ 消息总线集成

2. 群聊支持（优先级：高）
   □ 实现 @ 机器人检测（feishu_message_is_mention_bot 已实现）
   □ 添加群聊白名单配置
   □ Agent Loop 添加群聊过滤逻辑

3. 权限控制（优先级：高）
   □ 实现用户白名单（feishu_config.c）
   □ Agent Loop 添加权限检查

4. 富文本支持（优先级：中）
   □ Markdown → 飞书富文本转换
   □ 卡片消息增强（按钮、链接、图片等）

5. 富媒体支持（优先级：中）
   □ 图片上传与发送
   □ 文件上传与发送
   □ 语音上传与发送

6. 去重与可靠性（优先级：低）
   □ 消息去重机制（类似 Telegram）
   □ 错误重试与降级
```

---

## 6. 适合添加多模型支持的位置

### 6.1 当前多模型支持现状

**已支持**：
- ✅ Anthropic Messages API (`provider = "anthropic"`)
- ✅ OpenAI Chat Completions API (`provider = "openai"`)
- ✅ 自定义 Base URL（`base_url` 配置）

**限制**：
- ⚠️ 仅支持非流式响应
- ⚠️ 工具调用格式转换（Anthropic ↔ OpenAI）
- ⚠️ 无模型切换接口（需重启）

### 6.2 推荐扩展点

#### 6.2.1 模型配置层（已实现，可扩展）

**位置**：`main/llm/llm_proxy.c`

**当前实现**：
```c
static char s_model[LLM_MODEL_MAX_LEN] = MIMI_LLM_DEFAULT_MODEL;
static char s_provider[16] = MIMI_LLM_PROVIDER_DEFAULT;
static char s_base_url[256] = {0};
```

**建议扩展**：
1. **多模型配置**：支持配置多个模型，运行时切换
2. **模型元数据**：支持模型描述、能力标签（vision、tools、streaming 等）
3. **自动降级**：主模型失败时自动切换到备用模型

**代码示例**：
```c
// 模型配置结构
typedef struct {
    char name[64];                // 模型名称（如 "claude-3-opus"）
    char provider[16];            // "anthropic", "openai", "deepseek", ...
    char base_url[256];           // API Base URL（可选）
    bool supports_tools;           // 是否支持工具调用
    bool supports_vision;         // 是否支持视觉
    bool supports_streaming;      // 是否支持流式
    int max_tokens;               // 最大 token 数
    int priority;                 // 优先级（用于自动降级）
} llm_model_config_t;

// 模型注册表
#define LLM_MAX_MODELS  8
static llm_model_config_t s_models[LLM_MAX_MODELS];
static int s_model_count = 0;
static int s_current_model_index = 0;

// 运行时切换模型
esp_err_t llm_switch_model(const char *model_name)
{
    for (int i = 0; i < s_model_count; i++) {
        if (strcmp(s_models[i].name, model_name) == 0) {
            s_current_model_index = i;
            // 更新运行时配置
            safe_copy(s_model, sizeof(s_model), s_models[i].name);
            safe_copy(s_provider, sizeof(s_provider), s_models[i].provider);
            safe_copy(s_base_url, sizeof(s_base_url), s_models[i].base_url);
            ESP_LOGI(TAG, "Switched to model: %s (provider: %s)",
                     model_name, s_provider);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
```

#### 6.2.2 API 调用层（需扩展）

**位置**：`main/llm/llm_proxy.c` → `llm_http_call()`

**当前实现**：仅支持 HTTP POST 非流式请求

**建议扩展**：
1. **流式响应**：支持 SSE（Server-Sent Events）流式接收 token
2. **视觉模型**：支持图片输入（Claude Vision, GPT-4 Vision）
3. **音频模型**：支持 Whisper 语音转文字

**代码示例（流式响应）**：
```c
typedef void (*llm_stream_callback_t)(const char *token, void *user_data);

esp_err_t llm_chat_stream(const char *system_prompt,
                          cJSON *messages,
                          const char *tools_json,
                          llm_stream_callback_t callback,
                          void *user_data)
{
    // 1. 构造请求体（设置 stream: true）
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", s_model);
    cJSON_AddNumberToObject(body, "max_tokens", MIMI_LLM_MAX_TOKENS);
    cJSON_AddBoolToObject(body, "stream", true);  // 流式模式

    // ... 添加 system, messages, tools

    // 2. 发起 HTTP 请求（流式接收）
    resp_buf_t rb;
    resp_buf_init(&rb, MIMI_LLM_STREAM_BUF_SIZE);

    int status = 0;
    esp_err_t err = llm_http_stream_call(post_data, &rb, &status,
                                          callback, user_data);

    // 3. 解析 SSE 格式响应
    // data: {"delta": {"content": "Hello"}}
    // data: [DONE]
}

static esp_err_t parse_sse_chunk(const char *chunk, size_t len,
                                 llm_stream_callback_t callback, void *user_data)
{
    const char *p = chunk;
    while (p < chunk + len) {
        // 跳过 "data: " 前缀
        if (strncmp(p, "data: ", 6) == 0) {
            p += 6;
        }

        // 解析 JSON
        cJSON *json = cJSON_Parse(p);
        if (json) {
            cJSON *delta = cJSON_GetObjectItem(json, "delta");
            if (delta) {
                cJSON *content = cJSON_GetObjectItem(delta, "content");
                if (content && cJSON_IsString(content)) {
                    callback(content->valuestring, user_data);
                }
            }
            cJSON_Delete(json);
        }

        // 跳到下一行
        const char *newline = strchr(p, '\n');
        if (!newline) break;
        p = newline + 1;
    }
    return ESP_OK;
}
```

#### 6.2.3 工具调用兼容层（已实现，可增强）

**位置**：`main/llm/llm_proxy.c` → `convert_tools_openai()`

**当前实现**：Anthropic 格式 ↔ OpenAI 格式转换

**建议扩展**：
1. **DeepSeek 格式**：类似 OpenAI，支持工具调用
2. **Google Gemini 格式**：function calling
3. **Zhipu GLM 格式**：工具调用（与 Anthropic 类似）

**代码示例（DeepSeek）**：
```c
static bool provider_is_deepseek(void)
{
    return strcmp(s_provider, "deepseek") == 0;
}

static const char *llm_api_url(void)
{
    if (s_base_url[0] != '\0') {
        return s_base_url;
    }
    if (provider_is_deepseek()) {
        return "https://api.deepseek.com/chat/completions";
    }
    return provider_is_openai() ? MIMI_OPENAI_API_URL : MIMI_LLM_API_URL;
}

// DeepSeek 请求格式与 OpenAI 兼容，无需转换
static cJSON *convert_tools_deepseek(const char *tools_json)
{
    // DeepSeek 使用 OpenAI 格式，复用 convert_tools_openai()
    return convert_tools_openai(tools_json);
}
```

#### 6.2.4 模型降级策略（需新增）

**位置**：新增 `main/llm/llm_fallback.c`

**建议实现**：
1. **主模型失败**：自动切换到备用模型
2. **超时处理**：检测超时并切换
3. **错误分类**：区分临时错误（重试）和永久错误（切换）

**代码示例**：
```c
esp_err_t llm_chat_with_fallback(const char *system_prompt,
                                  cJSON *messages,
                                  const char *tools_json,
                                  llm_response_t *resp)
{
    int start_index = s_current_model_index;
    int attempts = 0;

    for (int i = 0; i < s_model_count; i++) {
        int model_idx = (start_index + i) % s_model_count;
        const llm_model_config_t *model = &s_models[model_idx];

        ESP_LOGI(TAG, "Attempting model %s (attempt %d)", model->name, attempts + 1);

        // 切换到当前模型
        llm_switch_model(model->name);

        // 调用 LLM
        esp_err_t err = llm_chat_tools(system_prompt, messages, tools_json, resp);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Success with model %s", model->name);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Model %s failed: %s", model->name, esp_err_to_name(err));

        // 检查是否应该继续尝试
        if (is_permanent_error(err)) {
            ESP_LOGW(TAG, "Permanent error, trying next model");
            attempts++;
        } else {
            // 临时错误，重试同一模型
            if (attempts < 2) {
                vTaskDelay(pdMS_TO_TICKS(1000));  // 1 秒延迟
                i--;  // 不切换模型
                attempts++;
            } else {
                attempts = 0;
            }
        }
    }

    ESP_LOGE(TAG, "All models failed");
    return ESP_FAIL;
}
```

### 6.3 多模型支持推荐顺序

```
1. 基础多模型支持（优先级：高）
   □ 模型配置结构（llm_model_config_t）
   □ 模型注册表（s_models[]）
   □ 运行时切换接口（llm_switch_model）

2. 更多模型提供商（优先级：高）
   □ DeepSeek（API 格式与 OpenAI 兼容）
   □ Zhipu GLM（API 格式与 Anthropic 类似）
   □ 自定义 OpenAI 兼容接口（base_url 配置）

3. 模型降级策略（优先级：中）
   □ 自动降级（主模型 → 备用模型）
   □ 错误分类（临时/永久）
   □ 超时处理

4. 流式响应（优先级：中）
   □ SSE 流式解析
   □ 流式回调接口
   □ Agent Loop 流式适配

5. 多模态支持（优先级：低）
   □ 视觉模型（图片输入）
   □ 音频模型（语音转文字）
   □ 媒体处理工具
```

### 6.4 模型配置文件示例

**位置**：`/spiffs/config/models.json`

```json
{
  "current_model": "claude-3-opus",
  "models": [
    {
      "name": "claude-3-opus",
      "provider": "anthropic",
      "supports_tools": true,
      "supports_vision": true,
      "supports_streaming": true,
      "max_tokens": 4096,
      "priority": 1
    },
    {
      "name": "claude-3-sonnet",
      "provider": "anthropic",
      "supports_tools": true,
      "supports_vision": true,
      "supports_streaming": true,
      "max_tokens": 4096,
      "priority": 2
    },
    {
      "name": "gpt-4-turbo",
      "provider": "openai",
      "supports_tools": true,
      "supports_vision": true,
      "supports_streaming": true,
      "max_tokens": 4096,
      "priority": 3
    },
    {
      "name": "deepseek-chat",
      "provider": "deepseek",
      "base_url": "https://api.deepseek.com/chat/completions",
      "supports_tools": true,
      "supports_vision": false,
      "supports_streaming": true,
      "max_tokens": 4096,
      "priority": 4
    }
  ]
}
```

---

## 7. 总结

### 7.1 MimiClaw 架构优势

1. **模块化设计**：消息总线、通道、Agent Loop、LLM Proxy 清晰分离
2. **多通道支持**：Telegram/Feishu/WebSocket 统一路由
3. **ReAct 工具调用**：支持多轮工具调用，灵活扩展
4. **嵌入式优化**：PSRAM 缓冲区、FreeRTOS 多核调度
5. **配置灵活**：编译时配置 + 运行时 NVS 覆盖

### 7.2 飞书接入建议

**核心接入点**：
- ✅ 消息接收层（`feishu_ws.c`）- 已实现 WebSocket
- ✅ 消息发送层（`feishu_send.c`）- 已实现 HTTP API
- ✅ 消息总线集成（`feishu.c`）- 已实现
- ⚠️ 群聊支持（`feishu_message.c`）- 需增强 @ 机器人检测
- ⚠️ 权限控制（`feishu_config.c`）- 需添加白名单
- ⚠️ 富文本支持（`feishu_send.c`）- 需添加 Markdown 转换

**推荐实现顺序**：群聊支持 → 权限控制 → 富文本 → 富媒体

### 7.3 多模型支持建议

**核心扩展点**：
- ✅ 多提供商支持（Anthropic/OpenAI）- 已实现
- ⚠️ 模型配置注册（`llm_proxy.c`）- 需添加模型注册表
- ⚠️ 运行时切换（`llm_proxy.c`）- 需添加切换接口
- ⚠️ 模型降级（新增 `llm_fallback.c`）- 需实现自动降级
- ⚠️ 流式响应（`llm_proxy.c`）- 需添加 SSE 解析

**推荐实现顺序**：模型配置 → 多提供商 → 模型降级 → 流式响应

### 7.4 技术债务与改进方向

1. **错误处理**：增强错误重试、降级策略
2. **流式响应**：支持 SSE 流式接收，提升用户体验
3. **多模态**：图片、语音输入支持
4. **性能优化**：JSON 解析、内存分配优化
5. **测试覆盖**：单元测试、集成测试

---

## 附录

### A. 关键文件索引

| 模块 | 文件 | 行数（约） |
|------|------|-----------|
| **消息总线** | `bus/message_bus.c` | 80 |
| **Telegram** | `telegram/telegram_bot.c` | 600 |
| **Feishu** | `feishu/*.c` | 400 |
| **Agent Loop** | `agent/agent_loop.c` | 350 |
| **LLM Proxy** | `llm/llm_proxy.c` | 700 |
| **工具注册** | `tools/tool_registry.c` | 300 |
| **会话管理** | `memory/session_mgr.c` | 250 |
| **HTTP 代理** | `proxy/http_proxy.c` | 400 |
| **上下文构建** | `agent/context_builder.c` | 200 |
| **主入口** | `mimi.c` | 180 |

### B. 配置常量索引

| 常量 | 值 | 说明 |
|------|-----|------|
| `MIMI_TG_POLL_TIMEOUT_S` | 30 | Telegram Polling 超时（秒） |
| `MIMI_TG_MAX_MSG_LEN` | 4096 | Telegram 消息最大长度 |
| `MIMI_AGENT_MAX_HISTORY` | 20 | 会话历史最大条数 |
| `MIMI_AGENT_MAX_TOOL_ITER` | 10 | 工具调用最大迭代次数 |
| `MIMI_MAX_TOOL_CALLS` | 4 | 单次响应最大工具调用数 |
| `MIMI_LLM_MAX_TOKENS` | 4096 | LLM 响应最大 token 数 |
| `MIMI_LLM_STREAM_BUF_SIZE` | 32 KB | LLM 响应缓冲区大小 |
| `MIMI_BUS_QUEUE_LEN` | 16 | 消息队列深度 |

### C. 参考资料

1. **Telegram Bot API**: https://core.telegram.org/bots/api
2. **飞书开放平台**: https://open.feishu.cn/
3. **Anthropic Messages API**: https://docs.anthropic.com/claude/reference/messages_post
4. **OpenAI Chat Completions API**: https://platform.openai.com/docs/api-reference/chat
5. **ESP-IDF 文档**: https://docs.espressif.com/projects/esp-idf/

---

**报告完成时间**: 2026-03-13
**分析工具**: 人工代码审查
**报告版本**: v1.0
