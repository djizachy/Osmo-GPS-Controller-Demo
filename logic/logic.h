#ifndef __LOGIC_H__
#define __LOGIC_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#include "enum.h"

/**
 * @brief 逻辑层初始化
 *
 * @param camera_name 相机设备名称，用于BLE连接
 * @return esp_err_t  ESP_OK 成功, 其他失败
 */
esp_err_t logic_init(const char *camera_name);

/**
 * @brief 切换相机模式
 *
 * @return esp_err_t  ESP_OK 成功, 其他失败
 */
esp_err_t logic_switch_camera_mode(camera_mode_t mode);

esp_err_t logic_get_version(void);

#endif // __LOGIC_H__
