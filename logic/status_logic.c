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

bool is_camera_recording() {
    if ((current_camera_status == CAMERA_STATUS_PHOTO_OR_RECORDING || current_camera_status == CAMERA_STATUS_PRE_RECORDING) && camera_status_initialized) {
        return true;
    }
    return false;
}

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

    ESP_LOGI(TAG, "Current camera status has changed:");
    ESP_LOGI(TAG, "  Mode: %s", mode_str);
    ESP_LOGI(TAG, "  Status: %s", status_str);
    ESP_LOGI(TAG, "  Resolution: %s", resolution_str);
    ESP_LOGI(TAG, "  FPS: %s", fps_str);
    ESP_LOGI(TAG, "  EIS: %s", eis_str);
}

int subscript_camera_status(uint8_t push_mode, uint8_t push_freq) {

    ESP_LOGI(TAG, "Subscribing to Camera Status with push_mode: %d, push_freq: %d", push_mode, push_freq);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return -1;
    }

    uint16_t seq = generate_seq();

    camera_status_subscription_command_frame command_frame = {
        .push_mode = push_mode,
        .push_freq = push_freq,
        .reserved = {0, 0, 0, 0}
    };

    send_command(0x1D, 0x05, CMD_NO_RESPONSE, &command_frame, seq, 5000);

    return 0;
}

/**
 * @brief 更新相机状态机
 * @param data
 */
void update_camera_state_handler(void *data) {
    if (!data) {
        ESP_LOGE(TAG, "logic_update_camera_state: Received NULL data.");
        return;
    }

    const camera_status_push_command_frame *parsed_data = (const camera_status_push_command_frame *)data;

    bool state_changed = false;

    // 检查并更新相机模式
    if (current_camera_mode != parsed_data->camera_mode) {
        current_camera_mode = parsed_data->camera_mode;
        ESP_LOGI(TAG, "Camera mode updated to: %d", current_camera_mode);
        state_changed = true;
    }

    // 检查并更新相机状态
    if (current_camera_status != parsed_data->camera_status) {
        current_camera_status = parsed_data->camera_status;
        ESP_LOGI(TAG, "Camera status updated to: %d", current_camera_status);
        state_changed = true;
    }

    // 检查并更新视频分辨率
    if (current_video_resolution != parsed_data->video_resolution) {
        current_video_resolution = parsed_data->video_resolution;
        ESP_LOGI(TAG, "Video resolution updated to: %d", current_video_resolution);
        state_changed = true;
    }

    // 检查并更新帧率
    if (current_fps_idx != parsed_data->fps_idx) {
        current_fps_idx = parsed_data->fps_idx;
        ESP_LOGI(TAG, "FPS index updated to: %d", current_fps_idx);
        state_changed = true;
    }

    // 检查并更新电子防抖模式
    if (current_eis_mode != parsed_data->eis_mode) {
        current_eis_mode = parsed_data->eis_mode;
        ESP_LOGI(TAG, "EIS mode updated to: %d", current_eis_mode);
        state_changed = true;
    }

    // 如果状态尚未初始化，标记为已初始化
    if (!camera_status_initialized) {
        camera_status_initialized = true;
        ESP_LOGI(TAG, "Camera state fully updated and marked as initialized.");
        state_changed = true;  // 强制打印状态，因为这是初始化
    }

    // 如果状态变更或第一次初始化，打印当前相机状态
    if (state_changed) {
        print_camera_status();
    }

    free(data);
}
