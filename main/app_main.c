#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "cJSON.h"
#include "logic.h"
#include "enum.h"

static const char *TAG = "APP_MAIN";

// 定时器句柄
static TimerHandle_t gps_timer_handle = NULL;

// 模拟的 GPS 数据结构
static int32_t year_month_day = 20250101;    // 年月日
static int32_t hour_minute_second = 150000;  // 时分秒
static int32_t gps_longitude = 116000000;    // 经度（单位：1E-7度）
static int32_t gps_latitude = 390000000;     // 纬度（单位：1E-7度）

// 设定初始速度和其他数据
static int32_t height = 8848000;             // 高度（单位：mm）
static uint32_t vertical_accuracy = 10;      // 垂直精度（单位：mm）
static uint32_t horizontal_accuracy = 10;    // 水平精度（单位：mm）
static uint32_t speed_accuracy = 10;         // 速度精度（单位：cm/s）
static uint32_t satellite_number = 5;        // 卫星数量

static float speed_to_north = 1000.0;        // 向北速度（单位：cm/s）
static float speed_to_east = 500.0;          // 向东速度（单位：cm/s）
static float speed_to_wnward = 200.0;        // 向下速度（单位：cm/s）

// 定时器回调函数，每次触发时模拟 GPS 数据
void gps_timer_callback(TimerHandle_t xTimer) {
    // 修改时间和位置数据来模拟连续数据
    hour_minute_second += 1;     // 每次增加 1 秒
    gps_latitude += 90;          // 纬度增加（模拟连续变化）
    gps_longitude += 58;         // 经度增加（模拟连续变化）

    // 发送伪造的 GPS 数据
    logic_push_gps_data(year_month_day, hour_minute_second, gps_longitude, gps_latitude,
                         height, speed_to_north, speed_to_east, speed_to_wnward,
                         vertical_accuracy, horizontal_accuracy, speed_accuracy, satellite_number);
}

void print_json_result(cJSON *json_result) {
    if (json_result) {
        char *result_str = cJSON_Print(json_result);
        if (result_str) {
            ESP_LOGI(TAG, "Received parse result: %s", result_str);
            free(result_str);  // 释放字符串内存
        }
        cJSON_Delete(json_result);  // 删除 JSON 对象
    } else {
        ESP_LOGE(TAG, "Received NULL JSON");
    }
}

void app_main(void) {
    esp_err_t ret;

    ret = logic_init("OsmoAction5Pro0C31");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Logic layer initialization failed, error: %s", esp_err_to_name(ret));
        // 你可以根据需要添加失败处理代码，比如重试或者终止程序等
    } else {
        ESP_LOGI(TAG, "Logic layer initialized successfully");
    }

    // 尝试连接相机
    uint32_t device_id = 0x12345702;                           // 示例设备ID
    uint8_t mac_addr_len = 6;                                  // 示例MAC地址长度
    int8_t mac_addr[6] = {0x37, 0x34, 0x56, 0x78, 0x9A, 0xBC}; // 示例MAC地址
    uint32_t fw_version = 0x00;                                // 示例固件版本
    uint8_t verify_mode = 1;                                   // 首次配对
    uint16_t verify_data = 8873;                               // 示例随机校验码
    uint8_t camera_reserved = 0;                               // 相机编号

    cJSON *connect_result = logic_connect(device_id, mac_addr_len, mac_addr, fw_version, verify_mode, verify_data, camera_reserved);
    if (connect_result == NULL) {
        ESP_LOGE(TAG, "Failed to connect to camera");
        return;
    }

    // 打印连接结果
    print_json_result(connect_result);

    // 获取设备版本信息并打印
    cJSON *json_result = logic_get_version();
    print_json_result(json_result);

    // 切换模式
    cJSON *json_result2 = logic_switch_camera_mode(CAMERA_MODE_NORMAL);
    print_json_result(json_result2);

    // 开始录制
    cJSON *json_result3 = logic_start_record();
    print_json_result(json_result3);

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

    /************************************************************************************************/

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

    // 停止录制
    cJSON *json_result4 = logic_stop_record();
    print_json_result(json_result4);

    // 断开连接
    logic_disconnect_camera();

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
