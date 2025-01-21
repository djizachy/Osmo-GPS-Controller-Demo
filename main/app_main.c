#include "freertos/FreeRTOS.h"

#include "connect_logic.h"
#include "gps_logic.h"
#include "key_logic.h"
#include "light_logic.h"

/**
 * @brief 应用主函数，执行初始化和任务循环
 *
 * 在此函数中，依次初始化氛围灯、GPS模块、蓝牙连接和按键逻辑，
 * 并启动一个循环任务，周期性进行操作。
 */
void app_main(void) {

    int res = 0;

    /* 初始化氛围灯 */
    res = init_light_logic();
    if (res != 0) {
        return;
    }

    /* 初始化 GPS 模块 */
    initSendGpsDataToCameraTask();

    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 初始化蓝牙 */
    res = connect_logic_ble_init();
    if (res != 0) {
        return;
    }

    /* 初始化按键逻辑 */
    key_logic_init();

    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
