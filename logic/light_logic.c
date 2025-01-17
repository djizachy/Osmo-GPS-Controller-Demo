#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

#include "connect_logic.h"
#include "status_logic.h"
#include "gps_logic.h"

#define TAG "LOGIC_LIGHT"

#define LED_GPIO 8                // 使用 GPIO 来控制 RGB LED
#define LED_STRIP_LENGTH 1        // 设定LED数量为1

// 创建一个 led_strip 句柄
static led_strip_handle_t led_strip = NULL;

// 初始化 RGB LED
static void init_rgb_led(void) {
    // 配置 LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_STRIP_LENGTH // 设置 LED 数量为 1
    };

    // 配置 RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 设置 RMT 分辨率为 10 MHz
        .flags.with_dma = false,           // 禁用 DMA
    };

    // 使用 &led_strip 作为第三个参数，因为它需要一个指向 led_strip_handle_t 的指针
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    led_strip_clear(led_strip); // 清除所有 LED，将其关闭
    ESP_LOGI(TAG, "RGB LED initialized");
}

// 设置RGB颜色
static void set_rgb_color(uint8_t red, uint8_t green, uint8_t blue) {
    led_strip_set_pixel(led_strip, 0, red, green, blue);  // 设置LED的颜色
    led_strip_refresh(led_strip); // 刷新 LED Strip 以更新颜色
}

// 在 light_logic.c 文件中定义全局变量
uint8_t led_red = 0, led_green = 0, led_blue = 0;  // RGB 值
bool led_blinking = false;                         // 是否闪烁
bool current_led_on = false;                       // LED 当前状态（开或关）

// 更新 LED 状态的函数
static void update_led_state() {
    connect_state_t current_connect_state = connect_logic_get_state();
    bool current_camera_recording = is_camera_recording();
    bool current_gps_connected = is_gps_found();

    // 打印当前状态
    // ESP_LOGI(TAG, "Current connect state: %d, Camera recording: %d, GPS connected: %d", current_connect_state, current_camera_recording, current_gps_connected);

    // 默认不闪烁
    led_blinking = false;

    switch (current_connect_state) {
        case BLE_NOT_INIT:
            led_red = 255;
            led_green = 0;
            led_blue = 0;  // 红色
            break;

        case BLE_INIT_COMPLETE:
            led_red = 255;
            led_green = 255;
            led_blue = 0;  // 黄色
            break;

        case BLE_SEARCHING:
            led_blinking = true;
            led_red = 0;
            led_green = 0;
            led_blue = 255;  // 蓝色闪烁
            break;

        case BLE_CONNECTED:
            led_red = 0;
            led_green = 0;
            led_blue = 255;  // 蓝色
            break;

        case PROTOCOL_CONNECTED:
            if (current_camera_recording) {
                if (current_gps_connected) {
                    led_blinking = true;
                    led_red = 128;
                    led_green = 0;
                    led_blue = 128;  // 紫色闪烁
                } else {
                    led_blinking = true;
                    led_red = 0;
                    led_green = 255;
                    led_blue = 0;  // 绿色闪烁
                }
            } else {
                if (current_gps_connected) {
                    led_red = 128;
                    led_green = 0;
                    led_blue = 128;  // 紫色
                } else {
                    led_red = 0;
                    led_green = 255;
                    led_blue = 0;  // 绿色
                }
            }
            break;
            
        default:
            led_red = 0;
            led_green = 0;
            led_blue = 0;  // 关闭 LED
            break;
    }
}

// 收集系统状态，设置灯参数的回调函数
static void led_state_timer_callback(TimerHandle_t xTimer) {
    update_led_state();
}

// 实现灯闪烁的回调函数
static void led_blink_timer_callback(TimerHandle_t xTimer) {
    if (led_blinking) {
        // 如果在闪烁状态，且当前 LED 开启，关闭 LED
        if (current_led_on) {
            set_rgb_color(0, 0, 0);  // 关闭 LED
        } else {
            // 如果当前 LED 关闭，设置为 RGB 颜色
            set_rgb_color(led_red, led_green, led_blue);
        }
        current_led_on = !current_led_on;
    } else {
        // 如果不在闪烁状态，直接设置为 RGB 颜色
        set_rgb_color(led_red, led_green, led_blue);
    }
}

int init_light_logic() {
    init_rgb_led();
    
    // 创建一个定时器，每 100ms 执行一次 update_led_state
    TimerHandle_t led_state_timer = xTimerCreate("led_state_timer", pdMS_TO_TICKS(100), pdTRUE, (void *)0, led_state_timer_callback);

    if (led_state_timer != NULL) {
        xTimerStart(led_state_timer, 0);
        ESP_LOGI(TAG, "LED state timer started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create LED state timer");
        return -1;
    }

    // 创建另一个定时器，用来控制 LED 闪烁的状态（开关）
    TimerHandle_t led_blink_timer = xTimerCreate("led_blink_timer", pdMS_TO_TICKS(800), pdTRUE, (void *)0, led_blink_timer_callback);

    if (led_blink_timer != NULL) {
        xTimerStart(led_blink_timer, 0);
        ESP_LOGI(TAG, "LED blink timer started successfully");
        return 0;
    } else {
        ESP_LOGE(TAG, "Failed to create LED blink timer");
        return -1;
    }
}
