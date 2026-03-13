#ifndef FEISHU_POLLING_H
#define FEISHU_POLLING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化飞书HTTP轮询
 *
 * @return ESP_OK 成功, ESP_FAIL 失败
 */
esp_err_t feishu_polling_init(void);

/**
 * @brief 启动轮询任务
 *
 * @return ESP_OK 成功, ESP_FAIL 失败
 */
esp_err_t feishu_polling_start(void);

/**
 * @brief 停止轮询任务
 *
 * @return ESP_OK 成功
 */
esp_err_t feishu_polling_stop(void);

/**
 * @brief 检查轮询是否正在运行
 *
 * @return true 运行中, false 已停止
 */
bool feishu_polling_is_running(void);

/**
 * @brief 检查是否已连接（成功获取到消息）
 *
 * @return true 已连接, false 未连接
 */
bool feishu_polling_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // FEISHU_POLLING_H
