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

    // 录制5秒，阻塞
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 停止录制
    cJSON *json_result4 = logic_stop_record();
    print_json_result(json_result4);

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
