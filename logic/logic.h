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

esp_err_t disconnect_camera(void);

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

// GPS信息推送
cJSON* logic_push_gps_data(int32_t year_month_day, int32_t hour_minute_second,
                            int32_t gps_longitude, int32_t gps_latitude,
                            int32_t height, float speed_to_north, float speed_to_east,
                            float speed_to_wnward, uint32_t vertical_accuracy,
                            uint32_t horizontal_accuracy, uint32_t speed_accuracy,
                            uint32_t satellite_number);

#endif // __LOGIC_H__
