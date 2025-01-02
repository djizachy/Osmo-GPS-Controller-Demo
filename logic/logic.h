#ifndef __LOGIC_H__
#define __LOGIC_H__

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
 * @param mode 相机模式
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* logic_switch_camera_mode(camera_mode_t mode);

// 获取版本号
cJSON* logic_get_version(void);

// 拍录控制
cJSON* logic_start_record(void);
cJSON* logic_stop_record(void);

#endif // __LOGIC_H__
