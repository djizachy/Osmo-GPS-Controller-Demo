#ifndef __LOGIC_H__
#define __LOGIC_H__

#include "cJSON.h"
#include "enum.h"

esp_err_t logic_init(const char *camera_name);

int logic_disconnect_camera(void);

cJSON* logic_switch_camera_mode(camera_mode_t mode);

cJSON* logic_get_version(void);

cJSON* logic_start_record(void);

cJSON* logic_stop_record(void);

cJSON* logic_push_gps_data(int32_t year_month_day, int32_t hour_minute_second,
                            int32_t gps_longitude, int32_t gps_latitude,
                            int32_t height, float speed_to_north, float speed_to_east,
                            float speed_to_wnward, uint32_t vertical_accuracy,
                            uint32_t horizontal_accuracy, uint32_t speed_accuracy,
                            uint32_t satellite_number);

cJSON* logic_connect(uint32_t device_id, uint8_t mac_addr_len, const int8_t *mac_addr,
                     uint32_t fw_version, uint8_t verify_mode, uint16_t verify_data,
                     uint8_t camera_reserved);

#endif // __LOGIC_H__
