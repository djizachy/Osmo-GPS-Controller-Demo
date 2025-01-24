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

#ifndef KEY_LOGIC_H
#define KEY_LOGIC_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define BOOT_KEY_GPIO   GPIO_NUM_9  // 假设 BOOT 按钮连接到 GPIO 9
                                    // Assume BOOT button is connected to GPIO 9

// 按键事件
// Key Events
typedef enum {
    KEY_EVENT_NONE = 0,   // 无事件
                          // No event
    KEY_EVENT_SINGLE,     // 单击事件
                          // Single click event
    KEY_EVENT_LONG_PRESS, // 长按事件
                          // Long press event
    KEY_EVENT_ERROR       // 错误事件
                          // Error event
} key_event_t;

void key_logic_init(void);

key_event_t key_logic_get_event(void);

#endif