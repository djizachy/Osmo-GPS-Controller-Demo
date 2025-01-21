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

// 按键事件变量，用于存储当前按键事件
static key_event_t current_key_event = KEY_EVENT_NONE;
// 按键状态，标记当前按键是否被按下
static bool key_pressed = false;
// 按键按下的起始时间，用于计算按下持续时间
static TickType_t key_press_start_time = 0;

// 长按阈值（例如：按下超过 1 秒认为是长按）
#define LONG_PRESS_THRESHOLD pdMS_TO_TICKS(1000)
// 单击与长按事件检测时间间隔（例如：50ms）
#define KEY_SCAN_INTERVAL pdMS_TO_TICKS(50)

/**
 * @brief 处理长按事件
 * 
 * 当按键被长时间按下时（超过长按阈值），执行相关的逻辑操作：
 * 1. 初始化数据层。
 * 2. 重新连接 BLE。
 * 3. 建立与相机的协议连接。
 * 4. 获取设备版本信息并订阅相机状态。
 */
static void handle_boot_long_press() {
    /* 初始化数据层 */
    if (!is_data_layer_initialized()) {
        ESP_LOGI(TAG, "Data layer not initialized, initializing now...");
        data_init(); 
        data_register_status_update_callback(update_camera_state_handler);
        if (!is_data_layer_initialized()) {
            ESP_LOGE(TAG, "Failed to initialize data layer");
            return;
        }
    }

    /* 重新连接 BLE */
    connect_state_t current_state = connect_logic_get_state();

    if (current_state > BLE_INIT_COMPLETE) {
        ESP_LOGI(TAG, "Current state is %d, disconnecting Bluetooth...", current_state);
        int res = connect_logic_ble_disconnect();
        if (res == -1) {
            ESP_LOGE(TAG, "Failed to disconnect Bluetooth.");
            return;
        }
    }

    ESP_LOGI(TAG, "Attempting to connect Bluetooth...");
    int res = connect_logic_ble_connect();
    if (res == -1) {
        ESP_LOGE(TAG, "Failed to connect Bluetooth.");
        return;
    } else {
        ESP_LOGI(TAG, "Successfully connected Bluetooth.");
    }

    /* 相机协议连接 */
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

    /* 获取设备版本信息并打印 */
    version_query_response_frame_t *version_response = command_logic_get_version();
    if (version_response != NULL) {
        free(version_response);
    }

    /* 订阅相机状态 */
    res = subscript_camera_status(PUSH_MODE_PERIODIC_WITH_STATE_CHANGE, PUSH_FREQ_10HZ);
    if (res == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to camera status.");
        return;
    } else {
        ESP_LOGI(TAG, "Successfully subscribed to camera status.");
    }
}

/**
 * @brief 处理单击事件
 * 
 * 当按键被单击时，执行以下操作：
 * 1. 获取当前相机模式。
 * 2. 如果相机正在直播，则启动录制。
 * 3. 如果相机正在录制，则停止录制。
 * 4. 切换相机至普通视频模式。
 */
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

    /* 切换相机至普通视频模式 */
    camera_mode_switch_response_frame_t *switch_response = command_logic_switch_camera_mode(CAMERA_MODE_NORMAL);
    if (switch_response != NULL) {
        free(switch_response);
    }

    /* QS 快速切换模式（可放入其他按键） */
    // key_report_response_frame_t *key_report_response = command_logic_key_report_qs();
    // if (key_report_response != NULL) {
    //     free(key_report_response);
    // }
}

/**
 * @brief 按键扫描任务
 * 
 * 定期检查按键状态，检测单击和长按事件，并触发相应的操作：
 * - 长按：进行蓝牙断开、重连、相机协议连接等操作。
 * - 单击：根据当前相机模式启动或停止录制，并切换相机模式。
 */
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
                // 处理长按事件：首先断开当前蓝牙连接，然后尝试重新连接
                handle_boot_long_press();
                // ESP_LOGI(TAG, "Long press detected. Duration: %lu ticks", press_duration);
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

/**
 * @brief 初始化按键逻辑
 * 
 * 配置按键的 GPIO 引脚，并启动按键扫描任务。
 */
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

/**
 * @brief 获取当前按键事件
 * 
 * 获取并重置当前的按键事件，主要用于外部任务获取事件后进行处理。
 * 
 * @return key_event_t 当前按键事件类型
 */
key_event_t key_logic_get_event(void) {
    key_event_t event = current_key_event;
    current_key_event = KEY_EVENT_NONE; // 获取后重置事件
    return event;
}
