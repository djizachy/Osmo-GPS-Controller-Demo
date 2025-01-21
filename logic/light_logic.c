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
#define LED_STRIP_LENGTH 1        // 设定 LED 数量为1

// 创建一个 led_strip 句柄
static led_strip_handle_t led_strip = NULL;

// 初始化 RGB LED 相关配置和设置
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

// 设置 RGB LED 的颜色
static void set_rgb_color(uint8_t red, uint8_t green, uint8_t blue) {
    led_strip_set_pixel(led_strip, 0, red, green, blue);  // 设置LED的颜色
    led_strip_refresh(led_strip); // 刷新 LED Strip 以更新颜色
}

// 初始化 RGB LED 状态所需的变量
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

    led_blinking = false;  // 默认 LED 不闪烁

    switch (current_connect_state) {
        case BLE_NOT_INIT:
            led_red = 13;      // 255 * 0.05
            led_green = 0;
            led_blue = 0;      // 红色表示蓝牙未初始化
            break;

        case BLE_INIT_COMPLETE:
            led_red = 13;      // 255 * 0.05
            led_green = 13;    // 255 * 0.05
            led_blue = 0;      // 黄色表示蓝牙初始化完成
            break;

        case BLE_SEARCHING:
            led_blinking = true;
            led_red = 0;
            led_green = 0;
            led_blue = 13;     // 255 * 0.05，蓝色闪烁表示蓝牙正在搜索
            break;

        case BLE_CONNECTED:
            led_red = 0;
            led_green = 0;
            led_blue = 13;     // 255 * 0.05，蓝色表示蓝牙已连接
            break;

        case PROTOCOL_CONNECTED:
            if (current_camera_recording) {
                if (current_gps_connected) {
                    led_blinking = true;
                    led_red = 6;       // 128 * 0.05
                    led_green = 0;
                    led_blue = 6;      // 紫色闪烁表示正在录制且 GPS 已连接
                } else {
                    led_blinking = true;
                    led_red = 0;
                    led_green = 13;    // 255 * 0.05
                    led_blue = 0;      // 绿色闪烁表示正在录制但 GPS 未连接
                }
            } else {
                if (current_gps_connected) {
                    led_red = 6;       // 128 * 0.05
                    led_green = 0;
                    led_blue = 6;      // 紫色表示未录制但 GPS 已连接
                } else {
                    led_red = 0;
                    led_green = 13;    // 255 * 0.05
                    led_blue = 0;      // 绿色表示未录制且 GPS 未连接
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

// 定时器回调函数，用于定期更新 LED 状态
static void led_state_timer_callback(TimerHandle_t xTimer) {
    update_led_state();
}

// 定时器回调函数，用于实现 LED 闪烁效果
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

// 初始化灯光逻辑，包括 LED 的状态更新和闪烁定时器
int init_light_logic() {
    init_rgb_led();
    
    // 创建一个定时器，每 500ms 执行一次 update_led_state
    TimerHandle_t led_state_timer = xTimerCreate("led_state_timer", pdMS_TO_TICKS(500), pdTRUE, (void *)0, led_state_timer_callback);

    if (led_state_timer != NULL) {
        xTimerStart(led_state_timer, 0);
        ESP_LOGI(TAG, "LED state timer started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create LED state timer");
        return -1;
    }

    // 创建另一个定时器，用来控制 LED 闪烁的状态（开关）
    TimerHandle_t led_blink_timer = xTimerCreate("led_blink_timer", pdMS_TO_TICKS(500), pdTRUE, (void *)0, led_blink_timer_callback);

    if (led_blink_timer != NULL) {
        xTimerStart(led_blink_timer, 0);
        ESP_LOGI(TAG, "LED blink timer started successfully");
        return 0;
    } else {
        ESP_LOGE(TAG, "Failed to create LED blink timer");
        return -1;
    }
}
