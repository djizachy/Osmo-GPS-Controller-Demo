#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include "enum.h"
#include "logic.h"
#include "data.h"
#include "ble.h"
#include "dji_protocol_parser.h"

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
 * @param root JSON 数据对象
 * @param seq 序列号
 * @param timeout_ms 等待结果的超时时间（毫秒）
 * @return cJSON* 成功返回解析结果的JSON对象，失败返回NULL
 */
static cJSON* send_command(uint8_t cmd_set, uint8_t cmd_id, cmd_type_t cmd_type, 
                           cJSON *root, uint16_t seq, int timeout_ms) {
    esp_err_t ret;

    // 打印生成的 JSON 数据，便于调试
    char *json_str = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Constructed JSON: %s", json_str);

    // 创建协议帧
    size_t frame_length_out = 0;
    uint8_t *protocol_frame = protocol_create_frame(cmd_set, cmd_id, cmd_type, root, seq, &frame_length_out);
    if (protocol_frame == NULL) {
        ESP_LOGE(TAG, "Failed to create protocol frame");
        cJSON_Delete(root);
        free(json_str);
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
        case CMD_TYPE_NO_RESPONSE:
            // 发送数据后不需要应答
            ret = data_write_without_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (no response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent without response.");
            break;

        case CMD_TYPE_WITH_RESPONSE_OR_NOT:
            // 发送数据后需要应答，等待结果但不报错
            ret = data_write_with_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (with response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for response...");
            
            // 等待解析结果（如果有结果则返回）
            ret = data_wait_for_result(seq, timeout_ms, &json_result);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "No result received, but continuing (seq=0x%04X)", seq);
                // 返回NULL表示没有解析结果，但不报错
            }

            break;

        case CMD_TYPE_WAIT_RESULT:
            // 发送数据后需要应答，等待结果并报错
            ret = data_write_with_response(seq, protocol_frame, frame_length_out);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (wait result), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return NULL;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for result...");

            // 等待解析结果
            ret = data_wait_for_result(seq, timeout_ms, &json_result);
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
    cJSON_AddStringToObject(root, "device_id", "\x33\xFF\x00\x00");
    char mode_data[1] = {(uint8_t)mode};
    cJSON_AddStringToObject(root, "mode", mode_data);
    cJSON_AddStringToObject(root, "reserved", "\x01\x47\x39\x36");

    // 调用通用函数并返回结果
    return send_command(0x1D, 0x04, CMD_TYPE_WITH_RESPONSE_OR_NOT, root, seq, 5000);
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
    return send_command(0x00, 0x00, CMD_TYPE_WAIT_RESULT, root, seq, 5000);
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
    cJSON_AddStringToObject(root, "device_id", "\x33\xFF\x00\x00"); // 假设固定设备 ID
    cJSON_AddStringToObject(root, "record_ctrl", "\x00");           // 0 表示开始录制
    cJSON_AddStringToObject(root, "reserved", "\x00\x00\x00\x00");  // 预留字段

    // 调用通用函数并返回结果
    return send_command(0x1D, 0x03, CMD_TYPE_WITH_RESPONSE_OR_NOT, root, seq, 5000);
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
    cJSON_AddStringToObject(root, "device_id", "\x33\xFF\x00\x00"); // 假设固定设备 ID
    cJSON_AddStringToObject(root, "record_ctrl", "\x01");          // 1 表示停止录制
    cJSON_AddStringToObject(root, "reserved", "\x00\x00\x00\x00"); // 预留字段

    // 调用通用函数并返回结果
    return send_command(0x1D, 0x03, CMD_TYPE_WITH_RESPONSE_OR_NOT, root, seq, 5000);
}