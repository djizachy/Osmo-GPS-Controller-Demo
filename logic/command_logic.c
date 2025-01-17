#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ble.h"
#include "data.h"
#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "status_logic.h"
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
 * @param cmd_set 命令集，用于指定命令的类别
 * @param cmd_id 命令 ID，用于标识具体命令
 * @param cmd_type 命令类型，指示是否需要应答等特性
 * @param structure 数据结构体指针，包含命令帧所需的输入数据
 * @param seq 序列号，用于匹配请求与响应
 * @param timeout_ms 等待结果的超时时间（以毫秒为单位）
 * @return CommandResult 成功返回解析后的结构体指针及数据长度，失败返回 NULL 指针及长度 0
 */
CommandResult send_command(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *input_raw_data, uint16_t seq, int timeout_ms) { 
    CommandResult result = { NULL, 0 };

    if(connect_logic_get_state() <= BLE_INIT_COMPLETE){
        ESP_LOGE(TAG, "BLE not connected");
        return result;
    }

    esp_err_t ret;

    // 创建协议帧
    size_t frame_length = 0;
    uint8_t *protocol_frame = protocol_create_frame(cmd_set, cmd_id, cmd_type, input_raw_data, seq, &frame_length);
    if (protocol_frame == NULL) {
        ESP_LOGE(TAG, "Failed to create protocol frame");
        return result;
    }

    ESP_LOGI(TAG, "Protocol frame created successfully, length: %zu", frame_length);

    // 打印 ByteArray 格式，便于调试
    printf("ByteArray: [");
    for (size_t i = 0; i < frame_length; i++) {
        printf("%02X", protocol_frame[i]);
        if (i < frame_length - 1) {
            printf(", ");
        }
    }
    printf("]\n");

    void *structure_data = NULL;
    size_t structure_data_length = 0;

    switch (cmd_type) {
        case CMD_NO_RESPONSE:
        case ACK_NO_RESPONSE:
            ret = data_write_without_response(seq, protocol_frame, frame_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (no response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return result;
            }
            ESP_LOGI(TAG, "Data frame sent without response.");
            break;

        case CMD_RESPONSE_OR_NOT:
        case ACK_RESPONSE_OR_NOT:
            ret = data_write_with_response(seq, protocol_frame, frame_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (with response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return result;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for response...");
            
            ret = data_wait_for_result_by_seq(seq, timeout_ms, &structure_data, &structure_data_length);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "No result received, but continuing (seq=0x%04X)", seq);
            }

            break;

        case CMD_WAIT_RESULT:
        case ACK_WAIT_RESULT:
            ret = data_write_with_response(seq, protocol_frame, frame_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (wait result), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return result;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for result...");

            ret = data_wait_for_result_by_seq(seq, timeout_ms, &structure_data, &structure_data_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get parse result for seq=0x%04X, error: 0x%x", seq, ret);
                free(protocol_frame);
                return result;
            }

            if (structure_data == NULL) {
                ESP_LOGE(TAG, "Parse result is NULL for seq=0x%04X", seq);
                free(protocol_frame);
                return result;
            }

            break;

        default:
            ESP_LOGE(TAG, "Invalid cmd_type: %d", cmd_type);
            free(protocol_frame);
            return result;
    }

    free(protocol_frame);
    ESP_LOGI(TAG, "Command executed successfully");

    result.structure = structure_data;
    result.length = structure_data_length;

    return result;
}

/**
 * @brief 切换相机模式
 *
 * @param mode 相机模式
 * @return camera_mode_switch_response_frame_t* 返回解析后的结构体指针，如果发生错误返回 NULL
 */
camera_mode_switch_response_frame_t* command_logic_switch_camera_mode(camera_mode_t mode) {
    ESP_LOGI(TAG, "%s: Switching camera mode to: %d", __FUNCTION__, mode);
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    camera_mode_switch_command_frame_t command_frame = {
        .device_id = 0x33FF0000,
        .mode = mode,
        .reserved = {0x01, 0x47, 0x39, 0x36}  // 预留字段
    };

    ESP_LOGI(TAG, "Constructed command frame: device_id=0x%08X, mode=%d", (unsigned int)command_frame.device_id, command_frame.mode);

    CommandResult result = send_command(
        0x1D,
        0x04,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    camera_mode_switch_response_frame_t *response = (camera_mode_switch_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Received response: ret_code=%d", response->ret_code);
    return response;
}

/**
 * @brief 查询设备版本号
 *
 * 该函数通过发送查询命令，获取设备的版本号信息。
 * 返回的版本号信息包括应答结果 (`ack_result`)、产品 ID (`product_id`) 和 SDK 版本号 (`sdk_version`)。
 * 注意：调用方需要在使用完返回的结构体后释放动态分配的内存。
 *
 * @return version_query_response_frame_t* 返回解析后的版本信息结构体，如果发生错误返回 NULL
 */
version_query_response_frame_t* command_logic_get_version(void) {
    ESP_LOGI(TAG, "%s: Querying device version", __FUNCTION__);
    
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    CommandResult result = send_command(
        0x00,
        0x00,
        CMD_WAIT_RESULT,
        NULL,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    version_query_response_frame_t *response = (version_query_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Version Query Response: ack_result=%u, product_id=%s, sdk_version=%.*s",
             response->ack_result, response->product_id, 
             (int)(result.length - (sizeof(uint16_t) + sizeof(response->product_id))),
             response->sdk_version);

    return response;
}

/**
 * @brief 开始录制
 *
 * @return record_control_response_frame_t* 返回解析后的应答结构体指针，如果发生错误返回 NULL
 */
record_control_response_frame_t* command_logic_start_record(void) {
    ESP_LOGI(TAG, "%s: Starting recording", __FUNCTION__);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    record_control_command_frame_t command_frame = {
        .device_id = 0x33FF0000,
        .record_ctrl = 0x00,
        .reserved = {0x00, 0x00, 0x00, 0x00}
    };

    CommandResult result = send_command(
        0x1D,
        0x03,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    record_control_response_frame_t *response = (record_control_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Start Record Response: ret_code=%d", response->ret_code);

    return response;
}

/**
 * @brief 停止录制
 *
 * @return record_control_response_frame_t* 返回解析后的应答结构体指针，如果发生错误返回 NULL
 */
record_control_response_frame_t* command_logic_stop_record(void) {
    ESP_LOGI(TAG, "%s: Stopping recording", __FUNCTION__);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    record_control_command_frame_t command_frame = {
        .device_id = 0x33FF0000,
        .record_ctrl = 0x01,
        .reserved = {0x00, 0x00, 0x00, 0x00}
    };

    CommandResult result = send_command(
        0x1D,
        0x03,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    record_control_response_frame_t *response = (record_control_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Stop Record Response: ret_code=%d", response->ret_code);

    return response;
}

/**
 * @brief 推送 GPS 数据
 *
 * @param gps_data 指向包含 GPS 数据的结构体
 * @return gps_data_push_response_frame* 返回解析后的应答结构体指针，如果发生错误返回 NULL
 */
gps_data_push_response_frame* command_logic_push_gps_data(const gps_data_push_command_frame *gps_data) {
    ESP_LOGI(TAG, "Pushing GPS data");

    // 检查连接状态
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    if (gps_data == NULL) {
        ESP_LOGE(TAG, "Invalid input: gps_data is NULL");
        return NULL;
    }

    uint16_t seq = generate_seq();

    // 发送命令并接收应答
    CommandResult result = send_command(
        0x00,
        0x17,
        CMD_NO_RESPONSE,
        gps_data,
        seq,
        5000
    );

    // 返回应答结构体指针
    return (gps_data_push_response_frame *)result.structure;
}