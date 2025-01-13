#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

#include "ble.h"
#include "data.h"
#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "dji_protocol_parser.h"
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC_COMMAND"

uint16_t s_current_seq = 0;

uint16_t generate_seq(void) {
    return s_current_seq += 1;
}

/**
 * @brief 构造数据帧并发送命令的通用函数
 *
 * @param cmd_set 命令集
 * @param cmd_id 命令 ID
 * @param cmd_type 命令类型
 * @param input_raw_data 数据对象
 * @param seq 序列号
 * @param timeout_ms 等待结果的超时时间（毫秒）
 * @param uint8_t create_mode
 * @return cJSON* 成功返回解析结果的JSON对象，失败返回NULL
 */
cJSON* send_command(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *input_raw_data, uint16_t seq, int timeout_ms, uint8_t create_mode) { 
    
    if(connect_logic_get_state() == CONNECT_STATE_DISCONNECTED){
        ESP_LOGE(TAG, "BLE not connected");
        return NULL;
    }

    if (!is_data_layer_initialized()) {
        ESP_LOGI(TAG, "Data layer not initialized, initializing now...");
        data_init();
        if (!is_data_layer_initialized()) {
            ESP_LOGE(TAG, "Failed to initialize data layer");
            return NULL;
        }
    }

    esp_err_t ret;

    if (create_mode == 0) {
        const cJSON *root = (const cJSON *)input_raw_data;
        if (root != NULL) {
            char *json_str = cJSON_PrintUnformatted(root);
            ESP_LOGI(TAG, "Constructed JSON: %s", json_str);
            free(json_str);
        } else {
            ESP_LOGE(TAG, "Invalid input: root is NULL in JSON mode");
            return NULL;
        }
    } else if (create_mode == 1) {
        if (input_raw_data == NULL) {
            ESP_LOGE(TAG, "Invalid input: input_data is NULL in structure mode");
            return NULL;
        }
        ESP_LOGI(TAG, "Using structure-based frame creation mode.");
    } else {
        ESP_LOGE(TAG, "Invalid create_mode: %d", create_mode);
        return NULL;
    }

    // 创建协议帧
    size_t frame_length_out = 0;
    uint8_t *protocol_frame = protocol_create_frame(cmd_set, cmd_id, cmd_type, input_raw_data, seq, &frame_length_out, create_mode);
    if (protocol_frame == NULL) {
        ESP_LOGE(TAG, "Failed to create protocol frame");
        if (create_mode == 0) {
            cJSON_Delete((cJSON *)input_raw_data);
        }
        return NULL;
    }

    ESP_LOGI(TAG, "Protocol frame created successfully, length: %zu", frame_length_out);

    // 打印 ByteArray 格式，便于调试
    printf("ByteArray: [");
    for (size_t i = 0; i < frame_length_out; i++) {
        printf("%02X", protocol_frame[i]);
        if (i < frame_length_out - 1) {
            printf(", ");
        }
    }
    printf("]\n");

    cJSON *json_result = NULL;

    switch (cmd_type) {
        case CMD_NO_RESPONSE:
        case ACK_NO_RESPONSE:
            ret = data_write_without_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (no response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent without response.");
            break;

        case CMD_RESPONSE_OR_NOT:
        case ACK_RESPONSE_OR_NOT:
            ret = data_write_with_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (with response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for response...");
            
            ret = data_wait_for_result_by_seq(seq, timeout_ms, &json_result);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "No result received, but continuing (seq=0x%04X)", seq);
            }

            break;

        case CMD_WAIT_RESULT:
        case ACK_WAIT_RESULT:
            ret = data_write_with_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (wait result), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for result...");

            ret = data_wait_for_result_by_seq(seq, timeout_ms, &json_result);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get parse result for seq=0x%04X, error: 0x%x", seq, ret);
                free(protocol_frame);
                return NULL;
            }

            if (json_result == NULL) {
                ESP_LOGE(TAG, "Parse result is NULL for seq=0x%04X", seq);
                free(protocol_frame);
                return NULL;
            }

            break;

        default:
            ESP_LOGE(TAG, "Invalid cmd_type: %d", cmd_type);
            free(protocol_frame);
            return NULL;
    }

    free(protocol_frame);
    ESP_LOGI(TAG, "Command executed successfully");

    return json_result ? json_result : NULL;
}

/**
 * @brief 切换相机模式
 *
 * @param mode 相机模式
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* command_logic_switch_camera_mode(camera_mode_t mode) {
    ESP_LOGI(TAG, "%s: Switching camera mode to: %d", __FUNCTION__, mode);
    if (connect_logic_get_state() != CONNECT_STATE_PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "33FF0000");      // 固定设备 ID
    char mode_data[3];
    sprintf(mode_data, "%02X", mode);
    cJSON_AddStringToObject(root, "mode", mode_data);            // 模式
    cJSON_AddStringToObject(root, "reserved", "01473936");       // 预留字段

    return send_command(0x1D, 0x04, CMD_RESPONSE_OR_NOT, root, seq, 5000, CREATE_MODE_CJSON);
}

/**
 * @brief 查询设备版本号
 *
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* command_logic_get_version(void) {
    ESP_LOGI(TAG, "%s: Querying device version", __FUNCTION__);
    if (connect_logic_get_state() != CONNECT_STATE_PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();
    cJSON *root = cJSON_CreateObject();

    return send_command(0x00, 0x00, CMD_WAIT_RESULT, root, seq, 5000, CREATE_MODE_CJSON);
}

/**
 * @brief 开始录制
 *
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* command_logic_start_record(void) {
    ESP_LOGI(TAG, "%s: Starting recording", __FUNCTION__);
    if (connect_logic_get_state() != CONNECT_STATE_PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "33FF0000");      // 假设固定设备 ID
    cJSON_AddStringToObject(root, "record_ctrl", "00");          // 0 表示开始录制
    cJSON_AddStringToObject(root, "reserved", "00000000");       // 预留字段

    return send_command(0x1D, 0x03, CMD_RESPONSE_OR_NOT, root, seq, 5000, CREATE_MODE_CJSON);
}

/**
 * @brief 停止录制
 *
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* command_logic_stop_record(void) {
    ESP_LOGI(TAG, "%s: Stopping recording", __FUNCTION__);
    if (connect_logic_get_state() != CONNECT_STATE_PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "33FF0000");      // 假设固定设备 ID
    cJSON_AddStringToObject(root, "record_ctrl", "01");          // 1 表示停止录制
    cJSON_AddStringToObject(root, "reserved", "00000000");       // 预留字段

    return send_command(0x1D, 0x03, CMD_RESPONSE_OR_NOT, root, seq, 5000, CREATE_MODE_CJSON);
}

cJSON* command_logic_push_gps_data(int32_t year_month_day, int32_t hour_minute_second,
                            int32_t gps_longitude, int32_t gps_latitude,
                            int32_t height, float speed_to_north, float speed_to_east,
                            float speed_to_wnward, uint32_t vertical_accuracy,
                            uint32_t horizontal_accuracy, uint32_t speed_accuracy,
                            uint32_t satellite_number) {
    // 踩坑：这里的 LOG 如果字符串过长，会造成定时器任务堆栈溢出
    ESP_LOGI(TAG, "Pushing GPS data");
    if (connect_logic_get_state() != CONNECT_STATE_PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    gps_data_push_command_frame gps_data = {
        .year_month_day = year_month_day,
        .hour_minute_second = hour_minute_second,
        .gps_longitude = gps_longitude,
        .gps_latitude = gps_latitude,
        .height = height,
        .speed_to_north = speed_to_north,
        .speed_to_east = speed_to_east,
        .speed_to_wnward = speed_to_wnward,
        .vertical_accuracy = vertical_accuracy,
        .horizontal_accuracy = horizontal_accuracy,
        .speed_accuracy = speed_accuracy,
        .satellite_number = satellite_number
    };

    return send_command(0x00, 0x17, CMD_NO_RESPONSE, &gps_data, seq, 5000, CREATE_MODE_STRUCT);
}
