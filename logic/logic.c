#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include "enum.h"
#include "logic.h"
#include "data.h"
#include "ble.h"
#include "dji_protocol_parser.h"
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC_LAYER"

/* 序列号生成器（简单自增） */
static uint16_t s_current_seq = 0;

/* 初始化逻辑层 */
esp_err_t logic_init(const char *camera_name) {
    esp_err_t ret;

    /* 1. 初始化数据层 */
    data_init();

    /* 2. 初始化 BLE（指定要搜索并连接的目标设备名） */
    ret = ble_init(camera_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE, error: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. 设置一个全局 Notify 回调，用于接收远端数据并进行协议解析 */
    ble_set_notify_callback(receive_camera_notify_handler); // 使用数据层的通知处理函数

    /* 4. 开始扫描并尝试连接 */
    ret = ble_start_scanning_and_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scanning and connect, error: 0x%x", ret);
        return ret;
    }

    /* 5. 等待最多 30 秒以确保 BLE 连接成功 */
    ESP_LOGI(TAG, "Waiting up to 30s for BLE to connect...");
    bool connected = false;
    for (int i = 0; i < 300; i++) { // 300 * 100ms = 10s
        if (s_ble_profile.connection_status.is_connected) {
            ESP_LOGI(TAG, "BLE connected successfully");
            connected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!connected) {
        ESP_LOGW(TAG, "BLE connection timed out");
        return ESP_ERR_TIMEOUT;
    }

    /* 6. 等待特征句柄查找完成（最多等待30秒） */
    ESP_LOGI(TAG, "Waiting up to 30s for characteristic handles discovery...");
    bool handles_found = false;
    for (int i = 0; i < 300; i++) { // 300 * 100ms = 30s
        if (s_ble_profile.handle_discovery.notify_char_handle_found && 
            s_ble_profile.handle_discovery.write_char_handle_found) {
            ESP_LOGI(TAG, "Required characteristic handles found");
            handles_found = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!handles_found) {
        ESP_LOGW(TAG, "Characteristic handles not found within timeout");
        return ESP_ERR_TIMEOUT;
    }

    /* 7. 注册通知 */
    ret = ble_register_notify(s_ble_profile.conn_id, s_ble_profile.notify_char_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register notify, error: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Logic layer initialized successfully");
    return ESP_OK;
}

int logic_disconnect_camera(void) {
    ESP_LOGI(TAG, "%s: Disconnecting camera", __FUNCTION__);

    // 调用 BLE 层的 ble_disconnect 函数
    esp_err_t ret = ble_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to disconnect camera, BLE error: %s", __FUNCTION__, esp_err_to_name(ret));
        return -1;  // 返回自定义失败状态码
    }

    ESP_LOGI(TAG, "%s: Camera disconnected successfully", __FUNCTION__);
    return 0;  // 返回自定义成功状态码
}

/**
 * @brief 生成下一个序列号
 *
 * @return uint16_t 下一个序列号
 */
static uint16_t generate_seq(void) {
    return ++s_current_seq;
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
static cJSON* send_command(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *input_raw_data, uint16_t seq, int timeout_ms, uint8_t create_mode) {
    esp_err_t ret;

    // 打印生成的 JSON 数据，便于调试
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
        // 如果是结构体模式，打印调试信息
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
            // 发送数据后不需要应答
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
            // 发送数据后需要应答，等待结果但不报错
            ret = data_write_with_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (with response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for response...");
            
            // 等待解析结果（如果有结果则返回）
            ret = data_wait_for_result_by_seq(seq, timeout_ms, &json_result);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "No result received, but continuing (seq=0x%04X)", seq);
                // 返回NULL表示没有解析结果，但不报错
            }

            break;

        case CMD_WAIT_RESULT:
        case ACK_WAIT_RESULT:
            // 发送数据后需要应答，等待结果并报错
            ret = data_write_with_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (wait result), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for result...");

            // 等待解析结果
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

    free(protocol_frame); // 释放帧内存
    ESP_LOGI(TAG, "Command executed successfully");

    // 返回解析结果（如果有的话）
    return json_result ? json_result : NULL;
}

/**
 * @brief 切换相机模式
 *
 * @param mode 相机模式
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* logic_switch_camera_mode(camera_mode_t mode) {
    ESP_LOGI(TAG, "%s: Switching camera mode to: %d", __FUNCTION__, mode);

    uint16_t seq = generate_seq();

    // 构造 JSON 数据
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "33FF0000");      // 假设固定设备 ID
    char mode_data[3];
    sprintf(mode_data, "%02X", mode);
    cJSON_AddStringToObject(root, "mode", mode_data);            // 模式
    cJSON_AddStringToObject(root, "reserved", "01473936");       // 预留字段

    // 调用通用函数并返回结果
    return send_command(0x1D, 0x04, CMD_RESPONSE_OR_NOT, root, seq, 5000, CREATE_MODE_CJSON);
}

/**
 * @brief 查询设备版本号
 *
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* logic_get_version(void) {
    ESP_LOGI(TAG, "%s: Querying device version", __FUNCTION__);

    uint16_t seq = generate_seq();
    cJSON *root = cJSON_CreateObject();
    return send_command(0x00, 0x00, CMD_WAIT_RESULT, root, seq, 5000, CREATE_MODE_CJSON);
}

/**
 * @brief 开始录制
 *
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* logic_start_record(void) {
    ESP_LOGI(TAG, "%s: Starting recording", __FUNCTION__);

    uint16_t seq = generate_seq();

    // 构造 JSON 数据
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "33FF0000");      // 假设固定设备 ID
    cJSON_AddStringToObject(root, "record_ctrl", "00");          // 0 表示开始录制
    cJSON_AddStringToObject(root, "reserved", "00000000");       // 预留字段

    // 调用通用函数并返回结果
    return send_command(0x1D, 0x03, CMD_RESPONSE_OR_NOT, root, seq, 5000, CREATE_MODE_CJSON);
}

/**
 * @brief 停止录制
 *
 * @return cJSON* 返回 JSON 数据，如果发生错误返回 NULL
 */
cJSON* logic_stop_record(void) {
    ESP_LOGI(TAG, "%s: Stopping recording", __FUNCTION__);

    uint16_t seq = generate_seq();

    // 构造 JSON 数据
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "33FF0000");      // 假设固定设备 ID
    cJSON_AddStringToObject(root, "record_ctrl", "01");          // 1 表示停止录制
    cJSON_AddStringToObject(root, "reserved", "00000000");       // 预留字段

    // 调用通用函数并返回结果
    return send_command(0x1D, 0x03, CMD_RESPONSE_OR_NOT, root, seq, 5000, CREATE_MODE_CJSON);
}

cJSON* logic_push_gps_data(int32_t year_month_day, int32_t hour_minute_second,
                            int32_t gps_longitude, int32_t gps_latitude,
                            int32_t height, float speed_to_north, float speed_to_east,
                            float speed_to_wnward, uint32_t vertical_accuracy,
                            uint32_t horizontal_accuracy, uint32_t speed_accuracy,
                            uint32_t satellite_number) {
    ESP_LOGI(TAG, "%s: Pushing GPS data", __FUNCTION__);

    // 生成序列号
    uint16_t seq = generate_seq();

    // 创建 gps_data_push_command_frame 结构体并填充数据
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

    // 直接调用 send_command，并传递结构体指针
    return send_command(0x00, 0x17, CMD_NO_RESPONSE, &gps_data, seq, 5000, CREATE_MODE_STRUCT);
}

cJSON* logic_connect(uint32_t device_id, uint8_t mac_addr_len, const int8_t *mac_addr,
                     uint32_t fw_version, uint8_t verify_mode, uint16_t verify_data,
                     uint8_t camera_reserved) {

    ESP_LOGI(TAG, "%s: Starting connection logic", __FUNCTION__);
    uint16_t seq = generate_seq();

    connection_request_command_frame connection_request = {
        .device_id = device_id,
        .mac_addr_len = mac_addr_len,
        .fw_version = fw_version,
        .verify_mode = verify_mode,
        .verify_data = verify_data,
    };
    memcpy(connection_request.mac_addr, mac_addr, mac_addr_len);

    // 2. 发送连接请求命令
    ESP_LOGI(TAG, "Sending connection request to camera...");
    cJSON *response = send_command(0x00, 0x19, CMD_WAIT_RESULT, &connection_request, seq, 5000, CREATE_MODE_STRUCT);
    if (!response) {
        ESP_LOGE(TAG, "Failed to send connection request");
        logic_disconnect_camera();
        return NULL;
    }

    // 解析相机返回的响应
    int ret_code = cJSON_GetObjectItem(response, "ret_code")->valueint;
    if (ret_code != 0) {
        ESP_LOGE(TAG, "Connection request rejected by camera, ret_code: %d", ret_code);
        cJSON_Delete(response);
        logic_disconnect_camera();
        return NULL;
    }

    ESP_LOGI(TAG, "Connection request accepted, waiting for camera to send connection command...");

    // 3. 等待相机发送连接请求
    cJSON *json_result = NULL;
    uint16_t received_seq = 0;
    esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 30000, &json_result, &received_seq);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timeout or error waiting for camera connection command");
        logic_disconnect_camera();
        return NULL;
    }

    // 解析相机发送的连接请求命令
    cJSON *verify_mode_item = cJSON_GetObjectItem(json_result, "verify_mode");
    cJSON *verify_data_item = cJSON_GetObjectItem(json_result, "verify_data");

    if (!verify_mode_item || !cJSON_IsNumber(verify_mode_item) ||
        !verify_data_item || !cJSON_IsNumber(verify_data_item)) {
        ESP_LOGE(TAG, "Invalid connection command format from camera");
        cJSON_Delete(json_result);
        logic_disconnect_camera();
        return NULL;
    }

    int camera_verify_mode = verify_mode_item->valueint;
    int camera_verify_data = verify_data_item->valueint;

    if (camera_verify_mode != 2) {
        ESP_LOGE(TAG, "Unexpected verify_mode from camera: %d", camera_verify_mode);
        cJSON_Delete(json_result);
        logic_disconnect_camera();
        return NULL;
    }

    if (camera_verify_data == 0) {
        ESP_LOGI(TAG, "Camera approved the connection, sending response...");

        // 构造连接应答帧
        connection_request_response_frame connection_response = {
            .device_id = device_id,
            .ret_code = 0,
        };
        memset(connection_response.reserved, 0, sizeof(connection_response.reserved));
        connection_response.reserved[0] = camera_reserved;

        ESP_LOGI(TAG, "构造完成，准备发送");

        // 最后一步发送
        send_command(0x00, 0x19, ACK_NO_RESPONSE, &connection_response, received_seq, 5000, CREATE_MODE_STRUCT);

        ESP_LOGI(TAG, "Connection successfully established with camera.");
    } else {
        ESP_LOGW(TAG, "Camera rejected the connection, closing Bluetooth link...");
        logic_disconnect_camera();
        cJSON_Delete(json_result);
        return NULL;
    }
    // 构造返回结果 JSON
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "connected");
    cJSON_AddNumberToObject(result, "camera_reserved", camera_reserved);

    cJSON_Delete(json_result);
    return result;
}