# Long-term Memory

(empty - MimiClaw will write memories here as it learns)

## GitHub Actions 构建问题解决方案 (2026-03-13)

### 问题
- 我们的 mimiclaw 仓库使用 ESP-IDF v5.5.2 容器构建失败
- 官方仓库（memovai/mimiclaw）相同配置但构建成功

### 根本原因
- 问题不在于配置文件（idf_component.yml、build.yml）
- 问题不在于 GitHub Actions 环境或 Component Manager
- 问题在于我们的代码与官方仓库的代码差异

### 关键发现
1. 官方仓库使用 WebSocket 实现 Feishu（channels/feishu/feishu_bot.c）
2. 我们的仓库使用 HTTP 轮询实现 Feishu（feishu/feishu_polling.c）
3. 官方仓库的代码能成功构建
4. 我们的代码导致构建失败

### 解决方案
- 使用官方仓库的代码替换我们的代码后，构建成功
- 需要逐步添加我们的自定义更改，并确保每次更改后都能成功构建
- 如果需要使用 HTTP 轮询，需要仔细修改代码以确保与 ESP-IDF v5.5.2 兼容

### 尝试过但无效的方法
1. 修改 idf_component.yml（添加/删除 esp_websocket_client 依赖）
2. 修改 CMakeLists.txt REQUIRES
3. 添加 Component Manager 环境变量
4. 添加调试步骤和日志上传
5. 注释掉 fullclean
6. 使用空的 idf_component.yml

### 验证
- Test workflow：✅ 成功
- Test Official Repo workflow：✅ 成功
- Build workflow（使用官方代码）：✅ 成功
