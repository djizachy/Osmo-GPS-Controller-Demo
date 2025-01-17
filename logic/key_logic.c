#include <time.h>
#include "key_logic.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "data.h"
#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "status_logic.h"
#include "dji_protocol_data_structures.h"

static const char *TAG = "LOGIC_KEY";

// 按键事件变量
static key_event_t current_key_event = KEY_EVENT_NONE;

// 按键状态
static bool key_pressed = false;
static TickType_t key_press_start_time = 0;

// 长按阈值（例如：按下超过 2 秒认为是长按）
#define LONG_PRESS_THRESHOLD pdMS_TO_TICKS(2000)

// 单击与长按事件检测时间间隔（例如：50ms）
#define KEY_SCAN_INTERVAL pdMS_TO_TICKS(50)

static void handle_boot_long_press() {
    // 初始化数据层
    if (!is_data_layer_initialized()) {
        ESP_LOGI(TAG, "Data layer not initialized, initializing now...");
        data_init(); 
        data_register_status_update_callback(update_camera_state_handler);
        if (!is_data_layer_initialized()) {
            ESP_LOGE(TAG, "Failed to initialize data layer");
            return;
        }
    }

    // 获取当前连接状态
    connect_state_t current_state = connect_logic_get_state();

    // 如果当前连接状态不是断开连接（即已连接），先断开连接
    if (current_state > BLE_INIT_COMPLETE) {
        ESP_LOGI(TAG, "Current state is %d, disconnecting Bluetooth...", current_state);

        int res = connect_logic_ble_disconnect();
        if (res == -1) {
            ESP_LOGE(TAG, "Failed to disconnect Bluetooth.");
            return;
        }
    }

    // 断开后，尝试重新连接 BLE
    ESP_LOGI(TAG, "Attempting to connect Bluetooth...");
    int res = connect_logic_ble_connect();
    if (res == -1) {
        ESP_LOGE(TAG, "Failed to connect Bluetooth.");
        return;
    } else {
        ESP_LOGI(TAG, "Successfully connected Bluetooth.");
    }

    // 相机连接请求
    uint32_t g_device_id = 0x12345703;                           // 示例设备ID
    uint8_t g_mac_addr_len = 6;                                  // MAC地址长度
    int8_t g_mac_addr[6] = {0x38, 0x34, 0x56, 0x78, 0x9A, 0xBC}; // 示例MAC地址
    uint32_t g_fw_version = 0x00;                                // 示例固件版本
    uint8_t g_verify_mode = 0;                                   // 首次配对
    uint16_t g_verify_data = 0;                                  // 随机校验码
    uint8_t g_camera_reserved = 0;                               // 相机编号

    srand((unsigned int)time(NULL));
    g_verify_data = (uint16_t)(rand() % 10000);
    res = connect_logic_protocol_connect(
        g_device_id,
        g_mac_addr_len,
        g_mac_addr,
        g_fw_version,
        g_verify_mode,
        g_verify_data,
        g_camera_reserved
    );
    if (res == -1) {
        ESP_LOGE(TAG, "Failed to connect to camera.");
        return;
    } else {
        ESP_LOGI(TAG, "Successfully connected to camera.");
    }

    // 订阅相机状态
    res = subscript_camera_status(PUSH_MODE_PERIODIC_WITH_STATE_CHANGE, PUSH_FREQ_10HZ);
    if (res == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to camera status.");
        return;
    } else {
        ESP_LOGI(TAG, "Successfully subscribed to camera status.");
    }
}

static void handle_boot_single_press() {
    // 获取当前相机模式
    camera_status_t current_status = current_camera_status;

    // 处理不同的相机状态
    if (current_status == CAMERA_STATUS_LIVE_STREAMING) {
        // 如果当前模式是直播，开始录制
        ESP_LOGI(TAG, "Camera is live streaming. Starting recording...");
        record_control_response_frame_t *start_record_response = command_logic_start_record();
        if (start_record_response != NULL) {
            ESP_LOGI(TAG, "Recording started successfully.");
            free(start_record_response);
        } else {
            ESP_LOGE(TAG, "Failed to start recording.");
        }
    } else if (is_camera_recording()) {
        // 如果当前模式是拍照或录制中，停止录制
        ESP_LOGI(TAG, "Camera is recording or pre-recording. Stopping recording...");
        record_control_response_frame_t *stop_record_response = command_logic_stop_record();
        if (stop_record_response != NULL) {
            ESP_LOGI(TAG, "Recording stopped successfully.");
            free(stop_record_response);
        } else {
            ESP_LOGE(TAG, "Failed to stop recording.");
        }
    } else {
        ESP_LOGI(TAG, "Camera is in an unsupported mode for recording.");
    }
}

// 按键扫描任务
static void key_scan_task(void *arg) {
    while (1) {
        // 获取按键状态
        bool new_key_state = gpio_get_level(BOOT_KEY_GPIO);

        if (new_key_state == 0 && !key_pressed) { // 按键按下
            key_pressed = true;
            key_press_start_time = xTaskGetTickCount();
            current_key_event = KEY_EVENT_NONE;
            // ESP_LOGI(TAG, "BOOT key pressed.");
        } else if (new_key_state == 0 && key_pressed) { // 按键保持按下状态
            TickType_t press_duration = xTaskGetTickCount() - key_press_start_time;

            if (press_duration >= LONG_PRESS_THRESHOLD && current_key_event != KEY_EVENT_LONG_PRESS) {
                // 长按事件（持续按下达到阈值时立即触发）
                current_key_event = KEY_EVENT_LONG_PRESS;
                ESP_LOGI(TAG, "Long press detected. Duration: %lu ticks", press_duration);
                // 处理长按事件：首先断开当前蓝牙连接，然后尝试重新连接
                handle_boot_long_press();
            }
        } else if (new_key_state == 1 && key_pressed) { // 按键松开
            key_pressed = false;
            TickType_t press_duration = xTaskGetTickCount() - key_press_start_time;

            if (press_duration < LONG_PRESS_THRESHOLD) {
                // 单击事件
                current_key_event = KEY_EVENT_SINGLE;
                ESP_LOGI(TAG, "Single press detected. Duration: %lu ticks", press_duration);
                // 处理单击事件：拍录控制
                handle_boot_single_press();
            }

            // 可以不做额外操作，因为长按的触发已经在按下过程中处理了
        }

        vTaskDelay(KEY_SCAN_INTERVAL);  // 每隔一段时间扫描一次按键状态
    }
}

void key_logic_init(void) {
    // 配置引脚为输入
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_KEY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&io_conf);

    // 启动按键扫描任务
    xTaskCreate(key_scan_task, "key_scan_task", 2048, NULL, 10, NULL);
}

key_event_t key_logic_get_event(void) {
    key_event_t event = current_key_event;
    current_key_event = KEY_EVENT_NONE; // 获取后重置事件
    return event;
}
