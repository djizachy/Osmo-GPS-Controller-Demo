#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "cJSON.h"

#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "gps_logic.h"

static const char *TAG = "MAIN";

static TimerHandle_t gps_timer_handle = NULL;

// 模拟的 GPS 数据
static int32_t year_month_day = 20250101;    // 年月日
static int32_t hour_minute_second = 150000;  // 时分秒
static int32_t gps_longitude = 116000000;    // 经度（单位：1E-7度）
static int32_t gps_latitude = 390000000;     // 纬度（单位：1E-7度）

static int32_t height = 8848000;             // 高度（单位：mm）
static uint32_t vertical_accuracy = 10;      // 垂直精度（单位：mm）
static uint32_t horizontal_accuracy = 10;    // 水平精度（单位：mm）
static uint32_t speed_accuracy = 10;         // 速度精度（单位：cm/s）
static uint32_t satellite_number = 5;        // 卫星数量

static float speed_to_north = 1000.0;        // 向北速度（单位：cm/s）
static float speed_to_east = 500.0;          // 向东速度（单位：cm/s）
static float speed_to_wnward = 200.0;        // 向下速度（单位：cm/s）

void gps_timer_callback(TimerHandle_t xTimer) {
    hour_minute_second += 1;                 // 每次增加 1 秒
    gps_latitude += 90;                      // 纬度增加
    gps_longitude += 58;                     // 经度增加

    command_logic_push_gps_data(year_month_day, hour_minute_second, gps_longitude, gps_latitude,
                         height, speed_to_north, speed_to_east, speed_to_wnward,
                         vertical_accuracy, horizontal_accuracy, speed_accuracy, satellite_number);
}

void print_json_result(cJSON *json_result) {
    if (json_result) {
        char *result_str = cJSON_Print(json_result);
        if (result_str) {
            ESP_LOGI(TAG, "Received parse result: %s", result_str);
            free(result_str);
        }
        cJSON_Delete(json_result);
    } else {
        ESP_LOGE(TAG, "Received NULL JSON");
    }
}

void app_main(void) {
    int res = 0;

    /* 1. BLE 连接相机 */
    res = connect_logic_ble_connect("OsmoAction5Pro0C31");
    if (res == -1) {
        return;
    }

    /* 2. 协议连接相机 */
    uint32_t device_id = 0x12345703;                           // 示例设备ID
    uint8_t mac_addr_len = 6;                                  // 示例MAC地址长度
    int8_t mac_addr[6] = {0x38, 0x34, 0x56, 0x78, 0x9A, 0xBC}; // 示例MAC地址
    uint32_t fw_version = 0x00;                                // 示例固件版本
    uint8_t verify_mode = 0;                                   // 首次配对
    uint16_t verify_data = 8874;                               // 示例随机校验码
    uint8_t camera_reserved = 0;                               // 相机编号

    res = connect_logic_protocol_connect(device_id, mac_addr_len, mac_addr, fw_version, verify_mode, verify_data, camera_reserved);
    if (res == -1) {
        return;
    }

    initSendGpsDataToCameraTask();

    /* 3. 获取设备版本信息并打印 */
    cJSON *json_result = command_logic_get_version();
    print_json_result(json_result);

    /* 4. 切换相机至普通视频模式 */
    cJSON *json_result2 = command_logic_switch_camera_mode(CAMERA_MODE_NORMAL);
    print_json_result(json_result2);

    /* 5. 开始录制 */
    cJSON *json_result3 = command_logic_start_record();
    print_json_result(json_result3);

    /* 6. GPS 推送示例 */
    /************************************************************************************************/

    // 创建定时器，设置为 100 毫秒触发一次，即 10Hz
    gps_timer_handle = xTimerCreate("GPS Timer", pdMS_TO_TICKS(100), pdTRUE, (void *)0, gps_timer_callback);
    if (gps_timer_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create GPS timer");
        return;
    }

    // 启动定时器
    if (xTimerStart(gps_timer_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start GPS timer");
        return;
    }

    // 假设录制的时间结束后，停止录制
    vTaskDelay(pdMS_TO_TICKS(20000)); // 等待 20 秒模拟数据推送

    // 停止定时器
    if (xTimerStop(gps_timer_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to stop GPS timer");
    }

    // 删除定时器，释放资源
    if (xTimerDelete(gps_timer_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to delete GPS timer");
    }

    /************************************************************************************************/

    /* 7. 停止录制 */
    cJSON *json_result4 = command_logic_stop_record();
    print_json_result(json_result4);

    /* 8. 断开BLE连接 */
    connect_logic_ble_disconnect();

    // // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
