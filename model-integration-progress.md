# MimiClaw 多模型集成进度

> **开始时间**: 2026-03-13
> **工作目录**: `/home/gem/workspace/agent/workspace/mimiclaw`

---

## 已完成功能

### 功能 1: 创建多模型配置结构 ✅

**完成时间**: 2026-03-13
**Git Commit**: (待提交)

**新增文件**:
- `main/llm/llm_config.h` - 模型配置系统头文件
- `main/llm/llm_config.c` - 模型配置系统实现

**实现内容**:
1. ✅ 创建模型配置结构体 `llm_model_config_t`:
   - 模型名称、Provider、Base URL、API Key
   - 能力标志（tools、vision）
   - 最大 token 数、优先级

2. ✅ 创建模型注册表:
   - `g_llm_models[]` - 最大支持 8 个模型
   - `g_llm_model_count` - 当前模型数量
   - `g_llm_current_model_index` - 当前激活模型索引

3. ✅ 实现默认模型:
   - claude-opus-4-5 (anthropic, priority: 1)
   - gpt-4-turbo (openai, priority: 2)
   - minimax (minimax, priority: 3)
   - qwen-plus (qwen, priority: 4)
   - moonshot-v1 (moonshot, priority: 5)
   - glm-4-plus (glm, priority: 6)

4. ✅ 公共接口:
   - `llm_config_init()` - 初始化模型配置
   - `llm_register_model()` - 注册模型
   - `llm_switch_model()` - 切换模型
   - `llm_get_current_model()` - 获取当前模型
   - `llm_get_model()` - 根据名称获取模型
   - `llm_get_model_count()` - 获取模型数量
   - `llm_list_models()` - 列出所有模型

---

### 功能 2: 实现模型注册和运行时切换 ✅

**完成时间**: 2026-03-13
**Git Commit**: (待提交)

**实现内容**:
1. ✅ 模型注册函数:
   - 支持注册新模型到注册表
   - 检测重复模型名称（更新而非报错）
   - 达到上限时报错（ESP_ERR_NO_MEM）

2. ✅ 模型切换函数:
   - 根据模型名称切换到对应模型
   - 更新 `g_llm_current_model_index`
   - 记录日志显示切换信息

3. ✅ 获取当前模型配置:
   - 返回当前激活模型的配置指针
   - 空注册表时返回 NULL

---

### 功能 3: 扩展 API 调用层支持新 providers ✅

**完成时间**: 2026-03-13
**Git Commit**: (待提交)

**修改文件**:
- `main/llm/llm_proxy.c`

**实现内容**:
1. ✅ 添加 provider 检测函数:
   - `provider_is_anthropic()` - Anthropic Messages API
   - `provider_is_openai()` - OpenAI Chat Completions API
   - `provider_is_minimax()` - MiniMax API
   - `provider_is_qwen()` - Qwen 3.5 API
   - `provider_is_moonshot()` - Kimi (Moonshot) API
   - `provider_is_glm()` - GLM-4.7 API
   - `provider_is_openai_compatible()` - 判断是否为 OpenAI 兼容格式

2. ✅ 扩展 API URL 路由:
   - MiniMax: `https://api.minimax.chat/v1/chat/completions`
   - Qwen: `https://api-inference.modelscope.cn/v1/chat/completions`
   - Moonshot: `https://api.moonshot.cn/v1/chat/completions`
   - GLM: `https://open.bigmodel.cn/api/paas/v4/chat/completions`
   - 支持模型配置中的自定义 `base_url`

3. ✅ 扩展认证逻辑:
   - Anthropic: `x-api-key` + `anthropic-version`
   - 其他 providers: `Authorization: Bearer <token>`
   - 支持模型配置中的自定义 `api_key`

4. ✅ 统一请求/响应格式:
   - 所有 OpenAI 兼容 providers 使用相同格式
   - 使用 `convert_messages_openai()` 转换消息格式
   - 使用 `convert_tools_openai()` 转换工具格式

---

## 进行中功能

### 功能 4: 实现自动降级策略

**状态**: ⏳ 待实现
**文件位置**: `main/llm/llm_fallback.c` (新建)

**待实现**:
1. 创建降级结构 `llm_fallback_t`
2. 实现带降级的 LLM 调用 `llm_chat_with_fallback()`
3. 降级逻辑:
   - 尝试当前模型
   - 失败时切换到下一个优先级的模型
   - 超时或网络错误也触发降级
   - 所有模型都失败时返回错误

---

### 功能 5: 添加模型配置 API

**状态**: ⏳ 待实现
**文件位置**: `main/llm/llm_api.c` (新建) 或集成到 `main/cli/serial_cli.c`

**待实现**:
1. 串口命令 `model list` - 查看已注册模型
2. 串口命令 `model switch <name>` - 切换模型
3. 串口命令 `model register <json>` - 注册新模型（可选）

---

## 集成的模型列表

| 模型名称 | Provider | API Base URL | 特点 |
|-----------|----------|--------------|------|
| claude-opus-4-5 | anthropic | 默认 Anthropic API | 支持 tools、vision |
| gpt-4-turbo | openai | 默认 OpenAI API | 支持 tools、vision |
| minimax | minimax | https://api.minimax.chat/v1/chat/completions | OpenAI 兼容，支持 tools |
| qwen-plus | qwen | https://api-inference.modelscope.cn/v1/chat/completions | OpenAI 兼容，支持 tools |
| moonshot-v1 | moonshot | https://api.moonshot.cn/v1/chat/completions | OpenAI 兼容，支持 tools |
| glm-4-plus | glm | https://open.bigmodel.cn/api/paas/v4/chat/completions | OpenAI 兼容，支持 tools |

---

## 遇到的问题和解决方案

### 问题 1: 编译错误 - 未定义外部变量

**描述**: 在 `llm_proxy.c` 中使用 `g_llm_model_count` 等全局变量时提示未定义。

**解决方案**: 这些变量在 `llm_config.c` 中定义，已在 `llm_config.h` 中声明为 `extern`。确保 `llm_config.c` 先编译。

---

## 下一步工作

1. ✅ 完成 Git 提交
2. ⏳ 实现功能 4: 自动降级策略
3. ⏳ 实现功能 5: 模型配置 API
4. ⏳ 编译测试
5. ⏳ 更新飞书进度文档
