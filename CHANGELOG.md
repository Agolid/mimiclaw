# Changelog

All notable changes to MimiClaw will be documented in this file.

## [Unreleased]

### Added
- **飞书集成增强**（Feishu Integration Enhancements）
  - 富媒体消息支持（图片、文件、语音）
  - 群聊 @ 机器人检测（仅处理 @ 机器人的群聊消息）
  - Markdown 转飞书富文本发送
  - 权限控制（用户白名单、群聊白名单）
- **多模型支持**（Multi-Model Support）
  - 支持 6 个 LLM Provider（Anthropic、OpenAI、MiniMax、Qwen、Kimi、GLM）
  - 运行时模型切换（CLI 命令：`model list`, `model switch <name>`）
  - 自动降级策略（主模型失败自动切换到备用模型）
  - 指数退避重试机制（1s、2s、4s...）
- **内存统计模块**（Memory Statistics Module）
  - 新增 `memory_stats` 模块用于监控内存使用
  - Agent Loop 定期输出内存统计（每 10 条消息）
  - 启动时记录完整内存信息
- **重试工具模块**（Retry Utilities Module）
  - 新增 `retry_utils` 模块提供通用的指数退避重试
  - 判断错误是否可重试（网络、超时、无内存等）
  - 详细的错误日志记录（ERROR_LOG_DETAIL 宏）

### Changed
- **统一消息处理接口**（Unified Message Processing Interface）
  - Telegram 消息现在包含完整的元数据（message_id、sender_id、parent_id）
  - 所有日志添加 `[Telegram]`/`[Feishu]` 前缀以便区分
  - Agent Loop 显示消息来源和详细信息
- **优化内存使用**（Optimize Memory Usage）
  - 飞书 HTTP 客户端使用 PSRAM 分配响应缓冲区
  - 所有大缓冲区优先使用 PSRAM
  - 添加内存使用统计和监控
- **完善错误处理**（Improve Error Handling）
  - 增强 outbound_dispatch_task 的错误处理和日志
  - 飞书发送函数使用详细错误日志
  - 判断错误是否可重试
  - 指数退避重试机制

### Fixed
- 修复消息去重问题（Telegram 的 update_offset + 缓存机制）
- 修复网络重试逻辑（自动降级策略）
- 优化 JSON 解析的缓冲区大小（使用 PSRAM）

### Technical Details
- **新增模块**：
  - `main/feishu/feishu_send.c` - 飞书消息发送
  - `main/llm/llm_config.c/h` - 多模型配置系统
  - `main/llm/llm_fallback.c/h` - 自动降级策略
  - `main/memory/memory_stats.c/h` - 内存统计模块
  - `main/common/retry_utils.c/h` - 重试工具模块
- **修改的模块**：
  - `main/bus/message_bus.h` - 扩展 `mimi_msg_t` 结构
  - `main/telegram/telegram_bot.c` - 填充完整消息元数据
  - `main/feishu/feishu.c` - 增强 Feishu 集成
  - `main/llm/llm_proxy.c` - 扩展 API 调用层
  - `main/agent/agent_loop.c` - 集成内存统计
  - `main/mimi.c` - 集成新模块

## [Previous Versions]

### Initial Release
- Telegram Bot 集成（Long Polling）
- Agent Loop（ReAct 模式）
- 消息总线系统
- 工具调用系统（web_search、get_current_time、cron）
- 会话管理和持久化
- WebSocket 网关
- OTA 更新支持
- 心跳服务
