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

#ifndef DJI_PROTOCOL_DATA_STRUCTURES_H
#define DJI_PROTOCOL_DATA_STRUCTURES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Define command and response frame structures
// 定义命令帧、应答帧结构体
typedef struct __attribute__((packed)) {
    uint32_t device_id;            // Device ID
                                   // 设备ID
    uint8_t mode;                  // Mode, refer to camera status in camera status push
                                   // 模式，参考相机状态推送中的相机状态
    uint8_t reserved[4];           // Reserved field
                                   // 保留字段
} camera_mode_switch_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // Return code: 0 for success, non-zero for failure
                                   // 返回码：0表示切换成功，非0表示切换失败
    uint8_t reserved[4];           // Reserved field
                                   // 保留字段
} camera_mode_switch_response_frame_t;

typedef struct __attribute__((packed)) {
    uint16_t ack_result;           // Acknowledgment result
                                   // 应答结果
    uint8_t product_id[16];        // Product ID, e.g., DJI-RS3
                                   // 产品ID，如 DJI-RS3
    uint8_t sdk_version[];         // SDK version data (flexible array)
                                   // sdk version 的数据（柔性数组）
} version_query_response_frame_t;

typedef struct __attribute__((packed)) {
    uint32_t device_id;            // Device ID
                                   // 设备ID
    uint8_t record_ctrl;           // Recording control: 0 - Start, 1 - Stop
                                   // 拍录控制：0 - 开始拍录，1 - 停止拍录
    uint8_t reserved[4];           // Reserved field
                                   // 预留字段
} record_control_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // Return code (refer to common return codes)
                                   // 返回码（参考普通返回码）
} record_control_response_frame_t;

typedef struct __attribute__((packed)) {
    int32_t year_month_day;        // Date (year*10000 + month*100 + day)
                                   // 年月日 (year*10000 + month*100 + day)
    int32_t hour_minute_second;    // Time ((hour+8)*10000 + minute*100 + second)
                                   // 时分秒 ((hour+8)*10000 + minute*100 + second)
    int32_t gps_longitude;         // Longitude (value = actual value * 10^7)
                                   // 经度 (value = 实际值 * 10^7)
    int32_t gps_latitude;          // Latitude (value = actual value * 10^7)
                                   // 纬度 (value = 实际值 * 10^7)
    int32_t height;                // Height in mm
                                   // 高度 单位：mm
    float speed_to_north;          // Speed to north in cm/s
                                   // 向北速度 单位：cm/s
    float speed_to_east;           // Speed to east in cm/s
                                   // 向东速度 单位：cm/s
    float speed_to_wnward;         // Speed downward in cm/s
                                   // 向下降速度 单位：cm/s
    uint32_t vertical_accuracy;    // Vertical accuracy estimate in mm
                                   // 垂直精度估计 单位：mm
    uint32_t horizontal_accuracy;  // Horizontal accuracy estimate in mm
                                   // 水平精度估计 单位：mm
    uint32_t speed_accuracy;       // Speed accuracy estimate in cm/s
                                   // 速度精度估计 单位：cm/s
    uint32_t satellite_number;     // Number of satellites
                                   // 卫星数量
} gps_data_push_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // Return code
                                   // 返回码
} gps_data_push_response_frame;

typedef struct __attribute__((packed)) {
    uint32_t device_id;            // Device ID
                                   // 设备ID
    uint8_t mac_addr_len;          // MAC address length
                                   // MAC地址长度
    int8_t mac_addr[16];           // MAC address
                                   // MAC地址
    uint32_t fw_version;           // Firmware version
                                   // 固件版本
    uint8_t conidx;                // Reserved field
                                   // 保留字段
    uint8_t verify_mode;           // Verification mode
                                   // 验证模式
    uint16_t verify_data;          // Verification data or result
                                   // 验证数据或结果
    uint8_t reserved[4];           // Reserved field
                                   // 预留字段
} connection_request_command_frame;

typedef struct __attribute__((packed)) {
    uint32_t device_id;            // Device ID
                                   // 设备ID
    uint8_t ret_code;              // Return code
                                   // 返回码
    uint8_t reserved[4];           // Reserved field
                                   // 预留字段
} connection_request_response_frame;

typedef struct __attribute__((packed)) {
    uint8_t push_mode;             // Push mode: 0-Off, 1-Single, 2-Periodic, 3-Periodic+Status change
                                   // 推送模式：0-关闭，1-单次，2-周期，3-周期+状态变化推送
    uint8_t push_freq;             // Push frequency in 0.1Hz
                                   // 推送频率，单位：0.1Hz
    uint8_t reserved[4];           // Reserved field
                                   // 预留字段
} camera_status_subscription_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t camera_mode;           // Camera mode: 0x00-Slow motion, 0x01-Normal mode, ...
                                   // 相机模式：0x00 - 慢动作模式，0x01 - 普通模式，...
    uint8_t camera_status;         // Camera status: 0x00-Screen off, 0x01-Live view, ...
                                   // 相机状态：0x00 - 屏幕关闭，0x01 - 直播，...
    uint8_t video_resolution;      // Video resolution: e.g., 0x10-1920x1080, ...
                                   // 视频分辨率：例如 0x10 - 1920x1080，...
    uint8_t fps_idx;               // Frame rate: e.g., 0x01-24fps, ...
                                   // 帧率：例如 0x01 - 24fps，...
    uint8_t eis_mode;              // Electronic image stabilization mode: 0-Off, 1-RS, ...
                                   // 电子防抖模式：0 - 关闭，1 - RS，...
    uint16_t record_time;          // Current recording time in seconds
                                   // 当前录像时间：单位秒
    uint8_t fov_type;              // FOV type, reserved field
                                   // FOV类型，保留字段
    uint8_t photo_ratio;           // Photo aspect ratio: 0-16:9, 1-4:3
                                   // 照片比例：0 - 16:9，1 - 4:3
    uint16_t real_time_countdown;  // Real-time countdown in seconds
                                   // 实时倒计时：单位秒
    uint16_t timelapse_interval;   // Time-lapse interval in 0.1s
                                   // 延时摄影时间间隔：单位0.1s
    uint16_t timelapse_duration;   // Time-lapse duration in seconds
                                   // 延时摄影时长：单位秒
    uint32_t remain_capacity;      // Remaining SD card capacity in MB
                                   // SD卡剩余容量：单位MB
    uint32_t remain_photo_num;     // Remaining number of photos
                                   // 剩余拍照张数
    uint32_t remain_time;          // Remaining recording time in seconds
                                   // 剩余录像时间：单位秒
    uint8_t user_mode;             // User mode: 0-General mode, 1-Custom mode 1, ...
                                   // 用户模式：0 - 通用模式，1 - 自定义模式1，...
    uint8_t power_mode;            // Power mode: 0-Normal working mode, 3-Sleep mode
                                   // 电源模式：0 - 正常工作模式，3 - 休眠模式
    uint8_t camera_mode_next_flag; // Pre-switch flag
                                   // 预切换标志
    uint8_t temp_over;             // Temperature status: 0-Normal, 1-Temperature warning, ...
                                   // 温度状态：0 - 正常，1 - 温度警告，...
    uint32_t photo_countdown_ms;   // Photo countdown parameter in milliseconds
                                   // 拍照倒计时参数：单位毫秒
    uint16_t loop_record_sends;    // Loop recording duration in seconds
                                   // 循环录像时长：单位秒
    uint8_t camera_bat_percentage; // Camera battery percentage: 0-100%
                                   // 相机电池电量：0~100%
} camera_status_push_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t key_code;              // Key code
                                   // 按键代码
    uint8_t mode;                  // Report mode: 0x00-Report key press/release status, 0x01-Report key events
                                   // 上报模式选择：0x00 上报按键按下/松开状态，0x01 上报按键事件
    uint16_t key_value;            // Key event value: For mode 0: 0x00-Key pressed, 0x01-Key released; For mode 1: 0x00-Short press, 0x01-Long press, 0x02-Double click, 0x03-Triple click, 0x04-Quadruple click
                                   // 按键事件值: 当 mode 为 0 时，0x00 按键按下，0x01 按键松开；当 mode 为 1 时，按键事件类型: 0x00 短按事件，0x01 长按事件，0x02 双击事件，0x03 三击事件，0x04 四击事件
} key_report_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;             // Return code (refer to common return codes)
                                  // 返回码（参考普通返回码）
} key_report_response_frame_t;

#endif