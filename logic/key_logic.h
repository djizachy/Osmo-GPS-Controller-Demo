#ifndef KEY_LOGIC_H
#define KEY_LOGIC_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define BOOT_KEY_GPIO   GPIO_NUM_9  // 假设 BOOT 按钮连接到 GPIO 9

// 按键事件
typedef enum {
    KEY_EVENT_NONE = 0,   // 无事件
    KEY_EVENT_SINGLE,     // 单击事件
    KEY_EVENT_LONG_PRESS, // 长按事件
    KEY_EVENT_ERROR       // 错误事件
} key_event_t;

void key_logic_init(void);

key_event_t key_logic_get_event(void);

#endif