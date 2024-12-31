#ifndef __DATA_H__
#define __DATA_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

/**
 * @brief 数据层初始化
 */
void data_init(void);

/**
 * @brief 发送数据帧（有响应）
 *
 * @param seq         序列号
 * @param data        数据指针
 * @param length      数据长度
 * @return esp_err_t  ESP_OK 成功, 其他失败
 */
esp_err_t data_write_with_response(uint16_t seq, const uint8_t *data, size_t length);

/**
 * @brief 发送数据帧（无响应）
 *
 * @param seq         序列号
 * @param data        数据指针
 * @param length      数据长度
 * @return esp_err_t  ESP_OK 成功, 其他失败
 */
esp_err_t data_write_without_response(uint16_t seq, const uint8_t *data, size_t length);

/**
 * @brief 等待特定序列号的解析结果
 *
 * @param seq         序列号
 * @param timeout_ms  超时时间（毫秒）
 * @param out_json    输出的 cJSON 对象指针
 * @return esp_err_t  ESP_OK 成功, ESP_ERR_TIMEOUT 超时, 其他失败
 */
esp_err_t data_wait_for_result(uint16_t seq, int timeout_ms, cJSON **out_json);

void receive_camera_notify_handler(const uint8_t *data, size_t length);

#endif // __DATA_H__
