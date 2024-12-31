#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "logic.h"
#include "enum.h"

// 日志标签
static const char *TAG = "APP_MAIN";


void app_main(void)
{
    esp_err_t ret;

    ret = logic_init("OsmoAction5Pro0C31");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Logic layer initialization failed, error: %s", esp_err_to_name(ret));
        // 你可以根据需要添加失败处理代码，比如重试或者终止程序等
    } else {
        ESP_LOGI(TAG, "Logic layer initialized successfully");
    }

    ret = logic_get_version();

    ret = logic_switch_camera_mode(CAMERA_MODE_TIMELAPSE_MOTION);  // TODO 根据枚举切换模式 BUG

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
