#include <time.h>
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
#include "status_logic.h"

static const char *TAG = "MAIN";

uint32_t g_device_id = 0x12345703;                           // 示例设备ID
uint8_t g_mac_addr_len = 6;                                  // MAC地址长度
int8_t g_mac_addr[6] = {0x38, 0x34, 0x56, 0x78, 0x9A, 0xBC}; // 示例MAC地址
uint32_t g_fw_version = 0x00;                                // 示例固件版本
uint8_t g_verify_mode = 0;                                   // 首次配对
uint16_t g_verify_data = 0;                                  // 随机校验码
uint8_t g_camera_reserved = 0;                               // 相机编号

void app_main(void) {
    int res = 0;

    /* 1. BLE 连接相机 */
    res = connect_logic_ble_connect("OsmoAction5Pro0C31");
    if (res == -1) {
        return;
    }

    /* 2. 协议连接相机 */
    srand((unsigned int)time(NULL)); // 设置随机数种子
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
        return;
    }

    /* 3. 订阅相机状态 */
    subscript_camera_status(3, 1);

    /* 4. 初始化 GPS 模块 */
    initSendGpsDataToCameraTask();

    /* 5. 获取设备版本信息并打印 */
    version_query_response_frame_t *version_response = command_logic_get_version();
    if (version_response != NULL) {
        free(version_response);
    }

    // 等待 GPS 信号
    while (!is_gps_found()) {
        ESP_LOGI(TAG, "Waiting for GPS signal...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* 6. 切换相机至普通视频模式 */
    camera_mode_switch_response_frame_t *switch_response = command_logic_switch_camera_mode(CAMERA_MODE_NORMAL);
    if (switch_response != NULL) {
        free(switch_response);
    }


    /* 7. 开始录制 */
    record_control_response_frame_t *start_record_response = command_logic_start_record();
    if (start_record_response != NULL) {
        free(start_record_response);
    }

    vTaskDelay(pdMS_TO_TICKS(30000));

    /* 8. 停止录制 */
    record_control_response_frame_t *stop_record_response = command_logic_stop_record();
    if (stop_record_response != NULL) {
        free(stop_record_response);
    }

    /* 9. 断开BLE连接 */
    connect_logic_ble_disconnect();

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每隔 5 秒重新发送一次
    }
}
