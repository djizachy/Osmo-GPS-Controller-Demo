#ifndef DJI_PROTOCOL_DATA_STRUCTURES_H
#define DJI_PROTOCOL_DATA_STRUCTURES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief 方式一：协议 DATA 字段描述结构体。
 */
typedef struct __attribute__((packed)) {
    const char *field_name; // 字段名称（便于调试和识别）
    size_t offset;          // 字段在帧中的偏移量（从 CmdSet 和 CmdID 后开始算）
    size_t size;            // 字段长度（字节）
    bool is_required;       // 是否为必须字段
} data_field_t;

// 声明模式切换命令帧和应答帧的字段描述符数组（extern 关键字声明，避免重复定义）
extern const data_field_t MODE_SWITCH_CM_DF[];
extern const data_field_t MODE_SWITCH_RE_DF[];
extern const data_field_t GET_VERSION_CM_DF[];
extern const data_field_t GET_VERSION_RE_DF[];
extern const data_field_t RECORD_CONTROL_CM_DF[];
extern const data_field_t RECORD_CONTROL_RE_DF[];

// 方式二：定义命令帧、应答帧结构体
typedef struct __attribute__((packed)) {
    int32_t year_month_day;            // 年月日 (year*10000 + month*100 + day)
    int32_t hour_minute_second;        // 时分秒 ((hour+8)*10000 + minute*100 + second)
    int32_t gps_longitude;             // 经度 (value = 实际值 * 10^7)
    int32_t gps_latitude;              // 纬度 (value = 实际值 * 10^7)
    int32_t height;                    // 高度 单位：mm
    float speed_to_north;              // 向北速度 单位：cm/s
    float speed_to_east;               // 向东速度 单位：cm/s
    float speed_to_wnward;             // 向下降速度 单位：cm/s
    uint32_t vertical_accuracy;        // 垂直精度估计 单位：mm
    uint32_t horizontal_accuracy;      // 水平精度估计 单位：mm
    uint32_t speed_accuracy;           // 速度精度估计 单位：cm/s
    uint32_t satellite_number;         // 卫星数量
} gps_data_push_command_frame;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;
} gps_data_push_response_frame;

typedef struct __attribute__((packed)) {
    uint32_t device_id;         // 设备ID
    uint8_t mac_addr_len;       // MAC地址长度
    int8_t mac_addr[16];        // MAC地址
    uint32_t fw_version;        // 固件版本
    uint8_t conidx;             // 保留字段
    uint8_t verify_mode;        // 验证模式
    uint16_t verify_data;       // 验证数据或结果
    uint8_t reserved[4];        // 预留字段
} connection_request_command_frame;

typedef struct __attribute__((packed)) {
    uint32_t device_id;         // 设备ID
    uint8_t ret_code;           // 返回码
    uint8_t reserved[4];        // 预留字段
} connection_request_response_frame;

#endif
