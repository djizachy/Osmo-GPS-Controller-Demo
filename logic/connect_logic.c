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
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC-CONNECT"

static connect_state_t connect_state = CONNECT_STATE_DISCONNECTED;

connect_state_t connect_logic_get_state(void) {
    return connect_state;
}

int connect_logic_ble_connect(const char *camera_name) {
    esp_err_t ret;

    /* 1. 初始化 BLE（指定要搜索并连接的目标设备名） */
    ret = ble_init(camera_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE, error: %s", esp_err_to_name(ret));
        return -1;
    }

    /* 2. 设置一个全局 Notify 回调，用于接收远端数据并进行协议解析 */
    ble_set_notify_callback(receive_camera_notify_handler); // 使用数据层的通知处理函数

    /* 3. 开始扫描并尝试连接 */
    ret = ble_start_scanning_and_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scanning and connect, error: 0x%x", ret);
        return -1;
    }

    /* 4. 等待最多 30 秒以确保 BLE 连接成功 */
    ESP_LOGI(TAG, "Waiting up to 30s for BLE to connect...");
    bool connected = false;
    for (int i = 0; i < 300; i++) { // 300 * 100ms = 30s
        if (s_ble_profile.connection_status.is_connected) {
            ESP_LOGI(TAG, "BLE connected successfully");
            connected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!connected) {
        ESP_LOGW(TAG, "BLE connection timed out");
        return -1;
    }

    /* 5. 等待特征句柄查找完成（最多等待30秒） */
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
        return -1;
    }

    /* 6. 注册通知 */
    ret = ble_register_notify(s_ble_profile.conn_id, s_ble_profile.notify_char_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register notify, error: %s", esp_err_to_name(ret));
        return -1;
    }

    // 更新状态为 BLE 已连接
    connect_state = CONNECT_STATE_BLE_CONNECTED;

    ESP_LOGI(TAG, "BLE connect successfully");
    return 0;
}

int connect_logic_ble_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting camera");

    // 调用 BLE 层的 ble_disconnect 函数
    esp_err_t ret = ble_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect camera, BLE error: %s", esp_err_to_name(ret));
        return -1;
    }

    // 更新状态为断开连接
    connect_state = CONNECT_STATE_DISCONNECTED;

    ESP_LOGI(TAG, "Camera disconnected successfully");
    return 0;
}

// 协议连接函数
int connect_logic_protocol_connect(uint32_t device_id, uint8_t mac_addr_len, const int8_t *mac_addr,
                                    uint32_t fw_version, uint8_t verify_mode, uint16_t verify_data,
                                    uint8_t camera_reserved) {
    ESP_LOGI(TAG, "%s: Starting protocol connection", __FUNCTION__);
    uint16_t seq = generate_seq();

    connection_request_command_frame connection_request = {
        .device_id = device_id,
        .mac_addr_len = mac_addr_len,
        .fw_version = fw_version,
        .verify_mode = verify_mode,
        .verify_data = verify_data,
    };
    memcpy(connection_request.mac_addr, mac_addr, mac_addr_len);

    // 发送连接请求命令
    ESP_LOGI(TAG, "Sending connection request to camera...");
    cJSON *response = send_command(0x00, 0x19, CMD_WAIT_RESULT, &connection_request, seq, 5000, CREATE_MODE_STRUCT);
    if (!response) {
        ESP_LOGE(TAG, "Failed to send connection request");
        connect_logic_ble_disconnect();
        return -1;
    }

    // 解析相机返回的响应
    int ret_code = cJSON_GetObjectItem(response, "ret_code")->valueint;
    if (ret_code != 0) {
        ESP_LOGE(TAG, "Connection request rejected by camera, ret_code: %d", ret_code);
        cJSON_Delete(response);
        connect_logic_ble_disconnect();
        return -1;
    }

    ESP_LOGI(TAG, "Connection request accepted, waiting for camera to send connection command...");

    // 等待相机发送连接请求
    cJSON *json_result = NULL;
    uint16_t received_seq = 0;
    esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 30000, &json_result, &received_seq);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timeout or error waiting for camera connection command");
        connect_logic_ble_disconnect();
        return -1;
    }

    // 解析相机发送的连接请求命令
    cJSON *verify_mode_item = cJSON_GetObjectItem(json_result, "verify_mode");
    cJSON *verify_data_item = cJSON_GetObjectItem(json_result, "verify_data");

    if (!verify_mode_item || !cJSON_IsNumber(verify_mode_item) || !verify_data_item || !cJSON_IsNumber(verify_data_item)) {
        ESP_LOGE(TAG, "Invalid connection command format from camera");
        cJSON_Delete(json_result);
        connect_logic_ble_disconnect();
        return -1;
    }

    int camera_verify_mode = verify_mode_item->valueint;
    int camera_verify_data = verify_data_item->valueint;

    if (camera_verify_mode != 2) {
        ESP_LOGE(TAG, "Unexpected verify_mode from camera: %d", camera_verify_mode);
        cJSON_Delete(json_result);
        connect_logic_ble_disconnect();
        return -1;
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

        ESP_LOGI(TAG, "Constructed connection response, sending...");

        // 发送连接应答帧
        send_command(0x00, 0x19, ACK_NO_RESPONSE, &connection_response, received_seq, 5000, CREATE_MODE_STRUCT);

        // 设置连接状态为协议连接
        connect_state = CONNECT_STATE_PROTOCOL_CONNECTED;

        ESP_LOGI(TAG, "Connection successfully established with camera.");
        cJSON_Delete(json_result);
        return 0;
    } else {
        ESP_LOGW(TAG, "Camera rejected the connection, closing Bluetooth link...");
        cJSON_Delete(json_result);
        connect_logic_ble_disconnect();
        return -1;
    }
}
