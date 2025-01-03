#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include "logic.h"
#include "enum.h"

static const char *TAG = "APP_MAIN";

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

    // 假设开始时间为 2025 年 1 月 1 日 12:00:00
    int32_t year_month_day = 20250101;    // 年月日
    int32_t hour_minute_second = 150000;  // 时分秒
    int32_t gps_longitude = 116000000;    // 经度（单位：1E-7度）
    int32_t gps_latitude = 390000000;     // 纬度（单位：1E-7度）

    // 假设高度、精度等初始值
    int32_t height = 8848000;             // 高度（单位：mm）
    uint32_t vertical_accuracy = 10;      // 垂直精度（单位：mm）
    uint32_t horizontal_accuracy = 10;    // 水平精度（单位：mm）
    uint32_t speed_accuracy = 10;         // 速度精度（单位：cm/s）
    uint32_t satellite_number = 5;        // 卫星数量

    // 设定初始速度
    float speed_to_north = 10000.0;          // 向北速度（单位：cm/s）
    float speed_to_east = 5000.0;            // 向东速度（单位：cm/s）
    float speed_to_wnward = 1.0;

    // 循环每秒伪造并发送 GPS 数据
    for (int i = 0; i < 20; i++) {  // 假设发送20次数据，每次1秒
        // 修改时间和位置数据来模拟连续数据
        hour_minute_second += 1;    // 每次增加 1 秒
        gps_latitude += 90;         // 纬度增加
        gps_longitude += 58;        // 经度增加

        // 发送伪造的 GPS 数据
        logic_push_gps_data(year_month_day, hour_minute_second, gps_longitude, gps_latitude,
                             height, speed_to_north, speed_to_east, speed_to_wnward,
                             vertical_accuracy, horizontal_accuracy, speed_accuracy, satellite_number);

        // 每秒发送一次数据
        vTaskDelay(pdMS_TO_TICKS(1000)); // 延时 1 秒
    }

    /************************************************************************************************/

    // 停止录制
    cJSON *json_result4 = logic_stop_record();
    print_json_result(json_result4);

    // 断开连接
    disconnect_camera();

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
