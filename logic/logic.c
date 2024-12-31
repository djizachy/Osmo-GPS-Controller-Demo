#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_log.h"

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
 * @brief 切换相机模式
 *
 * 构造数据帧，发送写命令，等待解析结果，并返回解析后的 cJSON 对象
 *
 * @return esp_err_t  ESP_OK 成功, 其他失败
 */
esp_err_t logic_switch_camera_mode(camera_mode_t mode) {
    esp_err_t ret;
    uint16_t seq = generate_seq();

    /* 1. 构造 key-value */
    cJSON *root = cJSON_CreateObject();

    // 构造 key-value 对（data 部分的内容）
    cJSON_AddStringToObject(root, "device_id", "\x33\xFF\x00\x00");  // 设备 ID
    cJSON_AddStringToObject(root, "mode", "\x0A"); // TODO
    cJSON_AddStringToObject(root, "reserved", "\x01\x47\x39\x36");   // 预留字段

    // 打印生成的 JSON 数据，便于调试
    char *json_str = cJSON_PrintUnformatted(root);  // 获取 JSON 字符串
    printf("构造 %s", json_str);

    /* 2. 调用协议帧创建函数 */
    uint8_t cmd_set = 0x1D;
    uint8_t cmd_id = 0x04;
    uint8_t cmd_type = 0x01;
    size_t frame_length_out = 0;

    uint8_t *protocol_frame = protocol_create_frame(cmd_set, cmd_id, cmd_type,
                                           root,  // 传入简单的 cJSON 数据对象
                                           seq, 
                                           &frame_length_out);
    if (protocol_frame == NULL) {
        ESP_LOGE(TAG, "Failed to create protocol frame");
        cJSON_Delete(root);
        free(json_str);
        return ESP_ERR_INVALID_ARG;
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

    /* 3. 发送数据帧（有响应） */
    ret = data_write_with_response(seq, protocol_frame, frame_length_out);
    free(protocol_frame); // 释放帧内存
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data frame, error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Data frame sent, waiting for response...");

    /* 4. 等待解析结果 */
    cJSON *json_result = NULL;
    ret = data_wait_for_result(seq, 5000, &json_result); // 等待 5 秒
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get parse result for seq=0x%04X, error: 0x%x", seq, ret);
        return ret;
    }

    /* 5. 处理解析结果 */
    if (json_result) {
        char *json_str = cJSON_Print(json_result);
        if (json_str) {
            ESP_LOGI(TAG, "Received parse result: %s", json_str);
            free(json_str);
        }
        cJSON_Delete(json_result); // 释放 cJSON 对象
    } else {
        ESP_LOGW(TAG, "Parse result is NULL for seq=0x%04X", seq);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Switch camera mode successfully");
    return ESP_OK;
}

/**
 * @brief 查询设备版本号
 *
 * 构造数据帧，发送写命令，等待解析结果，并返回解析后的 cJSON 对象
 *
 * @return esp_err_t  ESP_OK 成功, 其他失败
 */
esp_err_t logic_get_version(void) {
    esp_err_t ret;
    uint16_t seq = generate_seq();

    /* 1. 构造 key-value */
    cJSON *root = cJSON_CreateObject(); // 版本号查询无数据段，构造空的 cJSON 对象

    // 打印生成的 JSON 数据，便于调试
    char *json_str = cJSON_PrintUnformatted(root);  // 获取 JSON 字符串
    ESP_LOGI(TAG, "Constructed JSON for version query: %s", json_str);

    /* 2. 调用协议帧创建函数 */
    uint8_t cmd_set = 0x00;   // 版本号查询的 CmdSet
    uint8_t cmd_id = 0x00;    // 版本号查询的 CmdID
    uint8_t cmd_type = 0x01;  // 写命令
    size_t frame_length_out = 0;

    uint8_t *protocol_frame = protocol_create_frame(cmd_set, cmd_id, cmd_type,
                                                    root,  // 空的 cJSON 数据对象
                                                    seq, 
                                                    &frame_length_out);
    if (protocol_frame == NULL) {
        ESP_LOGE(TAG, "Failed to create protocol frame");
        cJSON_Delete(root);
        free(json_str);
        return ESP_ERR_INVALID_ARG;
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

    /* 3. 发送数据帧（有响应） */
    ret = data_write_with_response(seq, protocol_frame, frame_length_out);
    free(protocol_frame); // 释放帧内存
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data frame, error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Data frame sent, waiting for response...");

    /* 4. 等待解析结果 */
    cJSON *json_result = NULL;
    ret = data_wait_for_result(seq, 5000, &json_result); // 等待 5 秒
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get parse result for seq=0x%04X, error: 0x%x", seq, ret);
        return ret;
    }

    // /* 5. 处理解析结果 */
    if (json_result && cJSON_GetArraySize(json_result) > 0) {
        char *json_str = cJSON_Print(json_result);
        if (json_str) {
            ESP_LOGI(TAG, "Received parse result: %s", json_str);
            free(json_str);
        }
        cJSON_Delete(json_result); // 释放 cJSON 对象
    } else {
        ESP_LOGW(TAG, "Parse result is NULL for seq=0x%04X", seq);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Get version successfully");
    return ESP_OK;
}
