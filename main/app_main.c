/*
 * Copyright (c) 2025 DJI
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "freertos/FreeRTOS.h"

#include "connect_logic.h"
#include "gps_logic.h"
#include "key_logic.h"
#include "light_logic.h"

/**
 * @brief Main application function, performs initialization and task loop
 * 应用主函数，执行初始化和任务循环
 *
 * This function initializes the RGB light, GPS module, Bluetooth connection, 
 * and key logic in sequence, and starts a loop task for periodic operations.
 * 
 * 在此函数中，依次初始化氛围灯、GPS模块、蓝牙模块和按键逻辑，
 * 并启动一个循环任务，周期性进行操作。
 */
void app_main(void) {

    int res = 0;

    /* Initialize RGB light */
    /* 初始化氛围灯 */
    res = init_light_logic();
    if (res != 0) {
        return;
    }

    /* Initialize GPS module */
    /* 初始化 GPS 模块 */
    initSendGpsDataToCameraTask();

    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Initialize Bluetooth */
    /* 初始化蓝牙 */
    res = connect_logic_ble_init();
    if (res != 0) {
        return;
    }

    /* Initialize key logic */
    /* 初始化按键逻辑 */
    key_logic_init();

    /* Switch camera to normal video mode */
    /* 切换相机至普通视频模式 */
    // camera_mode_switch_response_frame_t *switch_response = command_logic_switch_camera_mode(CAMERA_MODE_NORMAL);
    // if (switch_response != NULL) {
    //     free(switch_response);
    // }

    // ===== Subsequent logic loop =====
    // ===== 后续逻辑循环 =====
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
