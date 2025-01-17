#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "gps_logic.h"
#include "key_logic.h"
#include "light_logic.h"

static const char *TAG = "MAIN";

void app_main(void) {
    int res = 0;

    /* 初始化氛围灯 */
    res = init_light_logic();
    if (res != 0) {
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 初始化蓝牙 */
    res = connect_logic_ble_init();
    if (res != 0) {
        return;
    }

    /* 初始化按键逻辑 */
    key_logic_init();

    // /* 初始化 GPS 模块 */
    // initSendGpsDataToCameraTask();

    // /* 获取设备版本信息并打印 */
    // version_query_response_frame_t *version_response = command_logic_get_version();
    // if (version_response != NULL) {
    //     free(version_response);
    // }

    // // 等待 GPS 信号
    // while (!is_gps_found()) {
    //     ESP_LOGI(TAG, "Waiting for GPS signal...");
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }

    // /* 切换相机至普通视频模式 */
    // camera_mode_switch_response_frame_t *switch_response = command_logic_switch_camera_mode(CAMERA_MODE_NORMAL);
    // if (switch_response != NULL) {
    //     free(switch_response);
    // }

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
