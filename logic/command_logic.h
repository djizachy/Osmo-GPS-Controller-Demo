#ifndef __COMMAND_LOGIC_H__
#define __COMMAND_LOGIC_H__

#include "cJSON.h"
#include "enums_logic.h"

uint16_t generate_seq(void);

cJSON* send_command(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *input_raw_data, uint16_t seq, int timeout_ms, uint8_t create_mode);

cJSON* command_logic_switch_camera_mode(camera_mode_t mode);

cJSON* command_logic_get_version(void);

cJSON* command_logic_start_record(void);

cJSON* command_logic_stop_record(void);

cJSON* command_logic_push_gps_data(int32_t year_month_day, int32_t hour_minute_second,
                            int32_t gps_longitude, int32_t gps_latitude,
                            int32_t height, float speed_to_north, float speed_to_east,
                            float speed_to_wnward, uint32_t vertical_accuracy,
                            uint32_t horizontal_accuracy, uint32_t speed_accuracy,
                            uint32_t satellite_number);

#endif
