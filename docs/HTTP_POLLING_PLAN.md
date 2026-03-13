# 飞书 HTTP 轮询替代 WebSocket

飞书的 WebSocket 功能不稳定，改用 HTTP 轮询更可靠。

## 实现方式

在 `feishu_ws.c` 中：
1. 删除所有 WebSocket 代码
2. 使用 `esp_http_client` 定期调用飞书 API：
   - `GET /open-apis/im/v1/messages?user_id_type=open_id&container_id_type=open_id&limit=20`
   - 轮询间隔：2-3 秒
3. 解析返回的 JSON，检查新消息

## 优点

- 不依赖 `esp_websocket_client` 组件
- 更稳定，连接失败自动重试
- ESP-IDF 5.5.2 完全支持

## 缺点

- 实时性稍差（有 2-3 秒延迟）
- 飞书 API 调用频率限制（需遵守官方限制）
