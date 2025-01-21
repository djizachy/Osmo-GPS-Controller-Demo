#ifndef DJI_PROTOCOL_DATA_STRUCTURES_H
#define DJI_PROTOCOL_DATA_STRUCTURES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 定义命令帧、应答帧结构体
typedef struct __attribute__((packed)) {
    uint32_t device_id;            // 设备ID
    uint8_t mode;                  // 模式，参考相机状态推送中的相机状态
    uint8_t reserved[4];           // 保留字段
} camera_mode_switch_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // 返回码：0表示切换成功，非0表示切换失败
    uint8_t reserved[4];           // 保留字段
} camera_mode_switch_response_frame_t;

typedef struct __attribute__((packed)) {
    uint16_t ack_result;           // 应答结果
    uint8_t product_id[16];        // 产品ID，如 DJI-RS3
    uint8_t sdk_version[];         // sdk version 的数据（柔性数组）
} version_query_response_frame_t;

typedef struct __attribute__((packed)) {
    uint32_t device_id;            // 设备ID
    uint8_t record_ctrl;           // 拍录控制：0 - 开始拍录，1 - 停止拍录
    uint8_t reserved[4];           // 预留字段
} record_control_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // 返回码（参考普通返回码）
} record_control_response_frame_t;

typedef struct __attribute__((packed)) {
    int32_t year_month_day;        // 年月日 (year*10000 + month*100 + day)
    int32_t hour_minute_second;    // 时分秒 ((hour+8)*10000 + minute*100 + second)
    int32_t gps_longitude;         // 经度 (value = 实际值 * 10^7)
    int32_t gps_latitude;          // 纬度 (value = 实际值 * 10^7)
    int32_t height;                // 高度 单位：mm
    float speed_to_north;          // 向北速度 单位：cm/s
    float speed_to_east;           // 向东速度 单位：cm/s
    float speed_to_wnward;         // 向下降速度 单位：cm/s
    uint32_t vertical_accuracy;    // 垂直精度估计 单位：mm
    uint32_t horizontal_accuracy;  // 水平精度估计 单位：mm
    uint32_t speed_accuracy;       // 速度精度估计 单位：cm/s
    uint32_t satellite_number;     // 卫星数量
} gps_data_push_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;
} gps_data_push_response_frame;

typedef struct __attribute__((packed)) {
    uint32_t device_id;            // 设备ID
    uint8_t mac_addr_len;          // MAC地址长度
    int8_t mac_addr[16];           // MAC地址
    uint32_t fw_version;           // 固件版本
    uint8_t conidx;                // 保留字段
    uint8_t verify_mode;           // 验证模式
    uint16_t verify_data;          // 验证数据或结果
    uint8_t reserved[4];           // 预留字段
} connection_request_command_frame;

typedef struct __attribute__((packed)) {
    uint32_t device_id;            // 设备ID
    uint8_t ret_code;              // 返回码
    uint8_t reserved[4];           // 预留字段
} connection_request_response_frame;

typedef struct __attribute__((packed)) {
    uint8_t push_mode;             // 推送模式：0-关闭，1-单次，2-周期，3-周期+状态变化推送
    uint8_t push_freq;             // 推送频率，单位：0.1Hz
    uint8_t reserved[4];           // 预留字段
} camera_status_subscription_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t camera_mode;           // 相机模式：0x00 - 慢动作模式，0x01 - 普通模式，...
    uint8_t camera_status;         // 相机状态：0x00 - 屏幕关闭，0x01 - 直播，...
    uint8_t video_resolution;      // 视频分辨率：例如 0x10 - 1920x1080，...
    uint8_t fps_idx;               // 帧率：例如 0x01 - 24fps，...
    uint8_t eis_mode;              // 电子防抖模式：0 - 关闭，1 - RS，...
    uint16_t record_time;          // 当前录像时间：单位秒
    uint8_t fov_type;              // FOV类型，保留字段
    uint8_t photo_ratio;           // 照片比例：0 - 16:9，1 - 4:3
    uint16_t real_time_countdown;  // 实时倒计时：单位秒
    uint16_t timelapse_interval;   // 延时摄影时间间隔：单位0.1s
    uint16_t timelapse_duration;   // 延时摄影时长：单位秒
    uint32_t remain_capacity;      // SD卡剩余容量：单位MB
    uint32_t remain_photo_num;     // 剩余拍照张数
    uint32_t remain_time;          // 剩余录像时间：单位秒
    uint8_t user_mode;             // 用户模式：0 - 通用模式，1 - 自定义模式1，...
    uint8_t power_mode;            // 电源模式：0 - 正常工作模式，3 - 休眠模式
    uint8_t camera_mode_next_flag; // 预切换标志
    uint8_t temp_over;             // 温度状态：0 - 正常，1 - 温度警告，...
    uint32_t photo_countdown_ms;   // 拍照倒计时参数：单位毫秒
    uint16_t loop_record_sends;    // 循环录像时长：单位秒
    uint8_t camera_bat_percentage; // 相机电池电量：0~100%
} camera_status_push_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t key_code;              // 按键代码
    uint8_t mode;                  // 上报模式选择：0x00 上报按键按下/松开状态，0x01 上报按键事件
    uint16_t key_value;            // 按键事件值: 当 mode 为 0 时，0x00 按键按下，0x01 按键松开；当 mode 为 1 时，按键事件类型: 0x00 短按事件，0x01 长按事件，0x02 双击事件，0x03 三击事件，0x04 四击事件
} key_report_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;             // 返回码（参考普通返回码）
} key_report_response_frame_t;

#endif