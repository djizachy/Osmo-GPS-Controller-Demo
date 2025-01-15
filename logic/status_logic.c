#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"

#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "dji_protocol_data_structures.h"

static const char *TAG = "LOGIC_STATUS";

uint8_t current_camera_mode = 0;
uint8_t current_camera_status = 0;
uint8_t current_video_resolution = 0;
uint8_t current_fps_idx = 0;
uint8_t current_eis_mode = 0;
bool camera_status_initialized = false;

void print_camera_status() {
    if (!camera_status_initialized) {
        ESP_LOGW(TAG, "Camera status has not been initialized.");
        return;
    }

    const char *mode_str = camera_mode_to_string((camera_mode_t)current_camera_mode);
    const char *status_str = camera_status_to_string((camera_status_t)current_camera_status);
    const char *resolution_str = video_resolution_to_string((video_resolution_t)current_video_resolution);
    const char *fps_str = fps_idx_to_string((fps_idx_t)current_fps_idx);
    const char *eis_str = eis_mode_to_string((eis_mode_t)current_eis_mode);

    ESP_LOGI(TAG, "Current Camera Status:");
    ESP_LOGI(TAG, "  Mode: %s", mode_str);
    ESP_LOGI(TAG, "  Status: %s", status_str);
    ESP_LOGI(TAG, "  Resolution: %s", resolution_str);
    ESP_LOGI(TAG, "  FPS: %s", fps_str);
    ESP_LOGI(TAG, "  EIS: %s", eis_str);
}

int subscript_camera_status(uint8_t push_mode, uint8_t push_freq) {

    ESP_LOGI(TAG, "Subscribing to Camera Status with push_mode: %d, push_freq: %d", push_mode, push_freq);

    if (connect_logic_get_state() != CONNECT_STATE_PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return -1;
    }

    uint16_t seq = generate_seq();

    camera_status_subscription_command_frame command_frame = {
        .push_mode = push_mode,
        .push_freq = push_freq,
        .reserved = {0, 0, 0, 0}
    };

    send_command(0x1D, 0x05, CMD_NO_RESPONSE, &command_frame, seq, 5000, CREATE_MODE_STRUCT);

    return 0;
}

/**
 * @brief 更新相机状态机
 * @param parsed_data 解析后的相机状态数据（cJSON）
 */
void update_camera_state_handler(cJSON *parsed_data) {
    if (!parsed_data) {
        ESP_LOGE(TAG, "logic_update_camera_state: Received NULL data.");
        return;
    }

    // 提取并更新相机模式
    cJSON *camera_mode_item = cJSON_GetObjectItem(parsed_data, "camera_mode");
    if (camera_mode_item && cJSON_IsNumber(camera_mode_item)) {
        current_camera_mode = (uint8_t)camera_mode_item->valueint;
        ESP_LOGI(TAG, "Camera mode updated to: %d", current_camera_mode);
    } else {
        ESP_LOGW(TAG, "Camera mode not found or invalid.");
    }

    // 提取并更新相机状态
    cJSON *camera_status_item = cJSON_GetObjectItem(parsed_data, "camera_status");
    if (camera_status_item && cJSON_IsNumber(camera_status_item)) {
        current_camera_status = (uint8_t)camera_status_item->valueint;
        ESP_LOGI(TAG, "Camera status updated to: %d", current_camera_status);
    } else {
        ESP_LOGW(TAG, "Camera status not found or invalid.");
    }

    // 提取并更新视频分辨率
    cJSON *video_resolution_item = cJSON_GetObjectItem(parsed_data, "video_resolution");
    if (video_resolution_item && cJSON_IsNumber(video_resolution_item)) {
        current_video_resolution = (uint8_t)video_resolution_item->valueint;
        ESP_LOGI(TAG, "Video resolution updated to: %d", current_video_resolution);
    } else {
        ESP_LOGW(TAG, "Video resolution not found or invalid.");
    }

    // 提取并更新帧率
    cJSON *fps_idx_item = cJSON_GetObjectItem(parsed_data, "fps_idx");
    if (fps_idx_item && cJSON_IsNumber(fps_idx_item)) {
        current_fps_idx = (uint8_t)fps_idx_item->valueint;
        ESP_LOGI(TAG, "FPS index updated to: %d", current_fps_idx);
    } else {
        ESP_LOGW(TAG, "FPS index not found or invalid.");
    }

    // 提取并更新电子防抖模式
    cJSON *eis_mode_item = cJSON_GetObjectItem(parsed_data, "EIS_mode");
    if (eis_mode_item && cJSON_IsNumber(eis_mode_item)) {
        current_eis_mode = (uint8_t)eis_mode_item->valueint;
        ESP_LOGI(TAG, "EIS mode updated to: %d", current_eis_mode);
    } else {
        ESP_LOGW(TAG, "EIS mode not found or invalid.");
    }

    // 执行状态切换后设置标志位
    if(camera_status_initialized == false) {
        camera_status_initialized = true;
        ESP_LOGI(TAG, "Camera state fully updated and marked as initialized.");
    }

    print_camera_status();
    cJSON_Delete(parsed_data);
}
