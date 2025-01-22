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
                                  // Use GPIO to control RGB LED
#define LED_STRIP_LENGTH 1        // 设定 LED 数量为1
                                  // Set the number of LEDs to 1

// 创建一个 led_strip 句柄
// Create a led_strip handle
static led_strip_handle_t led_strip = NULL;

// 初始化 RGB LED 相关配置和设置
// Initialize RGB LED related configurations and settings
static void init_rgb_led(void) {
    // 配置 LED
    // Configure LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_STRIP_LENGTH // 设置 LED 数量为 1
                                     // Set the number of LEDs to 1
    };

    // 配置 RMT
    // Configure RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 设置 RMT 分辨率为 10 MHz
                                           // Set RMT resolution to 10 MHz
        .flags.with_dma = false,           // 禁用 DMA
                                           // Disable DMA
    };

    // 使用 &led_strip 作为第三个参数，因为它需要一个指向 led_strip_handle_t 的指针
    // Use &led_strip as the third parameter because it needs a pointer to led_strip_handle_t
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    led_strip_clear(led_strip); // 清除所有 LED，将其关闭
                                // Clear all LEDs and turn them off
    ESP_LOGI(TAG, "RGB LED initialized");
}

// 设置 RGB LED 的颜色
// Set RGB LED color
static void set_rgb_color(uint8_t red, uint8_t green, uint8_t blue) {
    led_strip_set_pixel(led_strip, 0, red, green, blue);  // 设置LED的颜色
                                                          // Set LED color
    led_strip_refresh(led_strip); // 刷新 LED Strip 以更新颜色
                                  // Refresh LED Strip to update color
}

// 初始化 RGB LED 状态所需的变量
// Initialize variables needed for RGB LED status
uint8_t led_red = 0, led_green = 0, led_blue = 0;   // RGB 值
                                                    // RGB values
bool led_blinking = false;                          // 是否闪烁
                                                    // Whether to blink
bool current_led_on = false;                        // LED 当前状态（开或关）
                                                    // Current LED status (on or off)

// 更新 LED 状态的函数
// Function to update LED state
static void update_led_state() {
    connect_state_t current_connect_state = connect_logic_get_state();
    bool current_camera_recording = is_camera_recording();
    bool current_gps_connected = is_gps_found();

    // 打印当前状态
    // Print current status
    // ESP_LOGI(TAG, "Current connect state: %d, Camera recording: %d, GPS connected: %d", current_connect_state, current_camera_recording, current_gps_connected);

    led_blinking = false;  // 默认 LED 不闪烁
                           // LED does not blink by default

    switch (current_connect_state) {
        case BLE_NOT_INIT:
            led_red = 13;      // 255 * 0.05
            led_green = 0;
            led_blue = 0;      // 红色表示蓝牙未初始化
                               // Red indicates Bluetooth not initialized
            break;

        case BLE_INIT_COMPLETE:
            led_red = 13;      // 255 * 0.05
            led_green = 13;    // 255 * 0.05
            led_blue = 0;      // 黄色表示蓝牙初始化完成
                               // Yellow indicates Bluetooth initialization complete
            break;

        case BLE_SEARCHING:
            led_blinking = true;
            led_red = 0;
            led_green = 0;
            led_blue = 13;     // 255 * 0.05，蓝色闪烁表示蓝牙正在搜索
                               // Blue blinking indicates Bluetooth is searching
            break;

        case BLE_CONNECTED:
            led_red = 0;
            led_green = 0;
            led_blue = 13;     // 255 * 0.05，蓝色表示蓝牙已连接
                               // Blue indicates Bluetooth is connected
            break;

        case PROTOCOL_CONNECTED:
            if (current_camera_recording) {
                if (current_gps_connected) {
                    led_blinking = true;
                    led_red = 6;       // 128 * 0.05
                    led_green = 0;
                    led_blue = 6;      // 紫色闪烁表示正在录制且 GPS 已连接
                                       // Purple blinking indicates recording and GPS connected
                } else {
                    led_blinking = true;
                    led_red = 0;
                    led_green = 13;    // 255 * 0.05
                    led_blue = 0;      // 绿色闪烁表示正在录制但 GPS 未连接
                                       // Green blinking indicates recording but GPS not connected
                }
            } else {
                if (current_gps_connected) {
                    led_red = 6;       // 128 * 0.05
                    led_green = 0;
                    led_blue = 6;      // 紫色表示未录制但 GPS 已连接
                                       // Purple indicates not recording but GPS connected
                } else {
                    led_red = 0;
                    led_green = 13;    // 255 * 0.05
                    led_blue = 0;      // 绿色表示未录制且 GPS 未连接
                                       // Green indicates not recording and GPS not connected
                }
            }
            break;
            
        default:
            led_red = 0;
            led_green = 0;
            led_blue = 0;  // 关闭 LED
                           // Turn off LED
            break;
    }
}

// 定时器回调函数，用于定期更新 LED 状态
// Timer callback function for periodic LED state updates
static void led_state_timer_callback(TimerHandle_t xTimer) {
    update_led_state();
}

// 定时器回调函数，用于实现 LED 闪烁效果
// Timer callback function for LED blinking effect
static void led_blink_timer_callback(TimerHandle_t xTimer) {
    if (led_blinking) {
        // 如果在闪烁状态，且当前 LED 开启，关闭 LED
        // If in blinking state and LED is currently on, turn it off
        if (current_led_on) {
            set_rgb_color(0, 0, 0);  // 关闭 LED
                                     // Turn off LED
        } else {
            // 如果当前 LED 关闭，设置为 RGB 颜色
            // If LED is currently off, set it to RGB color
            set_rgb_color(led_red, led_green, led_blue);
        }
        current_led_on = !current_led_on;
    } else {
        // 如果不在闪烁状态，直接设置为 RGB 颜色
        // If not in blinking state, directly set to RGB color
        set_rgb_color(led_red, led_green, led_blue);
    }
}

// 初始化灯光逻辑，包括 LED 的状态更新和闪烁定时器
// Initialize light logic, including LED state updates and blink timer
int init_light_logic() {
    init_rgb_led();
    
    // 创建一个定时器，每 500ms 执行一次 update_led_state
    // Create a timer that executes update_led_state every 500ms
    TimerHandle_t led_state_timer = xTimerCreate("led_state_timer", pdMS_TO_TICKS(500), pdTRUE, (void *)0, led_state_timer_callback);

    if (led_state_timer != NULL) {
        xTimerStart(led_state_timer, 0);
        ESP_LOGI(TAG, "LED state timer started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create LED state timer");
        return -1;
    }

    // 创建另一个定时器，用来控制 LED 闪烁的状态（开关）
    // Create another timer to control LED blinking state (on/off)
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
