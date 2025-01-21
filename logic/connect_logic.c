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
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC_CONNECT"

static connect_state_t connect_state = BLE_NOT_INIT;

/**
 * @brief 获取当前连接状态
 * 
 * @return connect_state_t 返回当前的连接状态
 */
connect_state_t connect_logic_get_state(void) {
    return connect_state;
}

/**
 * @brief 处理相机断开连接（回调函数）
 * 
 * 根据当前连接状态进行相应的操作，并将连接状态重置为 BLE 初始化完成（BLE_INIT_COMPLETE）。
 */
void receive_camera_disconnect_handler() {
    switch (connect_state) {
        case BLE_SEARCHING:
            break;
        case BLE_INIT_COMPLETE:
            ESP_LOGI(TAG, "Already in DISCONNECTED state.");
            break;
        case BLE_DISCONNECTING: {
            ESP_LOGI(TAG, "Normal disconnection process.");
            // 正常断开也需要重置状态
            connect_state = BLE_INIT_COMPLETE;
            camera_status_initialized = false;
            ESP_LOGI(TAG, "Current state: DISCONNECTED.");
            break;
        }
        case BLE_CONNECTED:
        case PROTOCOL_CONNECTED:
        default: {
            ESP_LOGW(TAG, "Unexpected disconnection from state: %d, attempting reconnection...", connect_state);
            
            // 尝试重连一次
            bool reconnected = false;
            ESP_LOGI(TAG, "Reconnection attempt...");
            if (ble_reconnect() == ESP_OK) {
                // 等待重连结果
                for (int j = 0; j < 30; j++) { // 等待3秒
                    if (s_ble_profile.connection_status.is_connected) {
                        ESP_LOGI(TAG, "Reconnection successful");
                        reconnected = true;
                        return;  // 重连成功，直接返回
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }

            if (!reconnected) {
                ESP_LOGE(TAG, "Reconnection failed after 1 attempts");
                // 重连失败，执行断开逻辑
                connect_state = BLE_INIT_COMPLETE;
                camera_status_initialized = false;
                ble_disconnect();
                ESP_LOGI(TAG, "Current state: DISCONNECTED.");
            }
            break;
        }
    }
}

/**
 * @brief 初始化 BLE 连接
 * 
 * 初始化 BLE，并设置状态为 BLE 初始化完成（BLE_INIT_COMPLETE）。
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int connect_logic_ble_init() {
    esp_err_t ret;

    /* 1. 初始化 BLE（指定要搜索并连接的目标设备名） */
    ret = ble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE, error: %s", esp_err_to_name(ret));
        return -1;
    }

    connect_state = BLE_INIT_COMPLETE;
    ESP_LOGI(TAG, "BLE init successfully");
    return 0;
}

/**
 * @brief 连接到 BLE 设备
 * 
 * 执行以下步骤：设置回调、启动扫描并尝试连接、等待连接完成和特征句柄发现。
 * 如果连接失败，会返回错误并重置连接状态。
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int connect_logic_ble_connect() {
    connect_state = BLE_SEARCHING;

    esp_err_t ret;

    /* 1. 设置一个全局 Notify 回调，用于接收远端数据并进行协议解析 */
    ble_set_notify_callback(receive_camera_notify_handler);
    ble_set_state_callback(receive_camera_disconnect_handler);

    /* 2. 开始扫描并尝试连接 */
    ret = ble_start_scanning_and_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scanning and connect, error: 0x%x", ret);
        connect_state = BLE_INIT_COMPLETE;
        return -1;
    }

    /* 3. 等待最多 5 秒以确保 BLE 连接成功 */
    ESP_LOGI(TAG, "Waiting up to 30s for BLE to connect...");
    bool connected = false;
    for (int i = 0; i < 50; i++) { // 50 * 100ms = 5s
        if (s_ble_profile.connection_status.is_connected) {
            ESP_LOGI(TAG, "BLE connected successfully");
            connected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!connected) {
        ESP_LOGW(TAG, "BLE connection timed out");
        connect_state = BLE_INIT_COMPLETE;
        return -1;
    }

    /* 4. 等待特征句柄查找完成（最多等待30秒） */
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
        connect_state = BLE_INIT_COMPLETE;
        return -1;
    }

    /* 5. 注册通知 */
    ret = ble_register_notify(s_ble_profile.conn_id, s_ble_profile.notify_char_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register notify, error: %s", esp_err_to_name(ret));
        connect_state = BLE_INIT_COMPLETE;
        return -1;
    }

    // 更新状态为 BLE 已连接
    connect_state = BLE_CONNECTED;

    // 延迟展示氛围灯
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "BLE connect successfully");
    return 0;
}

/**
 * @brief 断开 BLE 连接
 * 
 * 尝试断开与 BLE 设备的连接。
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int connect_logic_ble_disconnect(void) {
    connect_state_t old_state = connect_state;
    connect_state = BLE_DISCONNECTING;
    
    ESP_LOGI(TAG, "Disconnecting camera");

    // 调用 BLE 层的 ble_disconnect 函数
    esp_err_t ret = ble_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect camera, BLE error: %s", esp_err_to_name(ret));
        connect_state = old_state;
        return -1;
    }

    ESP_LOGI(TAG, "Camera disconnected successfully");
    return 0;
}

/**
 * @brief 协议连接函数
 * 
 * 该函数负责建立协议连接，包含以下步骤：
 * 1. 向相机发送连接请求命令。
 * 2. 等待相机的响应并进行验证。
 * 3. 根据相机返回的命令发送连接应答。
 * 4. 设置连接状态为协议连接。
 * 
 * @param device_id 设备ID
 * @param mac_addr_len MAC地址长度
 * @param mac_addr 指向MAC地址的指针
 * @param fw_version 固件版本
 * @param verify_mode 验证模式
 * @param verify_data 验证数据
 * @param camera_reserved 相机保留字段
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int connect_logic_protocol_connect(uint32_t device_id, uint8_t mac_addr_len, const int8_t *mac_addr,
                                   uint32_t fw_version, uint8_t verify_mode, uint16_t verify_data,
                                   uint8_t camera_reserved) {
    ESP_LOGI(TAG, "%s: Starting protocol connection", __FUNCTION__);
    uint16_t seq = generate_seq();

    // 构造连接请求命令帧
    connection_request_command_frame connection_request = {
        .device_id = device_id,
        .mac_addr_len = mac_addr_len,
        .fw_version = fw_version,
        .verify_mode = verify_mode,
        .verify_data = verify_data,
    };
    memcpy(connection_request.mac_addr, mac_addr, mac_addr_len);


    // STEP1: 相机发送连接请求命令
    ESP_LOGI(TAG, "Sending connection request to camera...");
    CommandResult result = send_command(0x00, 0x19, CMD_WAIT_RESULT, &connection_request, seq, 1000);

    /****************** 连接问题，这里相机可能返回 connection_request_response_frame 也可能返回命令帧 ******************/

    if (result.structure == NULL) {
        // 这里直接去 esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 30000, &received_seq, &parse_result, &parse_result_length);
        // 如果 != OK 说明确实没有收到消息，超时
        // 否则 GOTO 到 wait_for_camera_command 标识
        void *parse_result = NULL;
        size_t parse_result_length = 0;
        uint16_t received_seq = 0;
        esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 1000, &received_seq, &parse_result, &parse_result_length);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Timeout or error waiting for camera connection command, GOTO Failed.");
            connect_logic_ble_disconnect();
            return -1;
        } else {
            // 如果能收到数据，跳过解析相机返回响应，直接进入STEP2
            goto wait_for_camera_command;
        }
    }

    // 解析相机返回的响应
    connection_request_response_frame *response = (connection_request_response_frame *)result.structure;
    if (response->ret_code != 0) {
        ESP_LOGE(TAG, "Connection request rejected by camera, ret_code: %d", response->ret_code);
        free(response);
        connect_logic_ble_disconnect();
        return -1;
    }

    ESP_LOGI(TAG, "Connection request accepted, waiting for camera to send connection command...");
    free(response);

    // STEP2: 等待相机发送连接请求
wait_for_camera_command:
    void *parse_result = NULL;
    size_t parse_result_length = 0;
    uint16_t received_seq = 0;
    esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 30000, &received_seq, &parse_result, &parse_result_length);

    if (ret != ESP_OK || parse_result == NULL) {
        ESP_LOGE(TAG, "Timeout or error waiting for camera connection command");
        connect_logic_ble_disconnect();
        return -1;
    }

    // 解析相机发送的连接请求命令
    connection_request_command_frame *camera_request = (connection_request_command_frame *)parse_result;

    if (camera_request->verify_mode != 2) {
        ESP_LOGE(TAG, "Unexpected verify_mode from camera: %d", camera_request->verify_mode);
        free(parse_result);
        connect_logic_ble_disconnect();
        return -1;
    }

    if (camera_request->verify_data == 0) {
        ESP_LOGI(TAG, "Camera approved the connection, sending response...");

        // 构造连接应答帧
        connection_request_response_frame connection_response = {
            .device_id = device_id,
            .ret_code = 0,
        };
        memset(connection_response.reserved, 0, sizeof(connection_response.reserved));
        connection_response.reserved[0] = camera_reserved;

        ESP_LOGI(TAG, "Constructed connection response, sending...");

        // STEP3: 发送连接应答帧
        send_command(0x00, 0x19, ACK_NO_RESPONSE, &connection_response, received_seq, 5000);

        // 设置连接状态为协议连接
        connect_state = PROTOCOL_CONNECTED;

        ESP_LOGI(TAG, "Connection successfully established with camera.");
        free(parse_result);
        return 0;
    } else {
        ESP_LOGW(TAG, "Camera rejected the connection, closing Bluetooth link...");
        free(parse_result);
        connect_logic_ble_disconnect();
        return -1;
    }
}
