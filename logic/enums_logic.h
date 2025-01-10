#ifndef __ENUMS_LOGIC_H__
#define __ENUMS_LOGIC_H__

typedef enum {
    CMD_NO_RESPONSE = 0x00,       // 命令帧 - 发送数据后不需要应答
    CMD_RESPONSE_OR_NOT = 0x01,   // 命令帧 - 发送数据后需要应答，没收到结果不报错
    CMD_WAIT_RESULT = 0x02,       // 命令帧 - 发送数据后需要应答，没收到结果会报错

    ACK_NO_RESPONSE = 0x20,       // 应答帧 - 不需要应答 (00100000)
    ACK_RESPONSE_OR_NOT = 0x21,   // 应答帧 - 需要应答，没收到结果不报错 (00100001)
    ACK_WAIT_RESULT = 0x22        // 应答帧 - 需要应答，没收到结果会报错 (00100010)
} cmd_type_t;

typedef enum {
    CAMERA_MODE_SLOW_MOTION = 0x00,       // 慢动作模式
    CAMERA_MODE_NORMAL = 0x01,            // 普通模式
    CAMERA_MODE_TIMELAPSE_STATIC = 0x02,  // 延时摄影模式 1 静止延时
    CAMERA_MODE_PHOTO = 0x05,             // 普通拍照模式
    CAMERA_MODE_TIMELAPSE_MOTION = 0x0A,  // 延时摄影模式 2 运动延时
    CAMERA_MODE_LIVE_STREAMING = 0x1A,    // 直播模式
    CAMERA_MODE_UVC_STREAMING = 0x23,     // UVC 直播模式
    CAMERA_MODE_LOW_LIGHT_VIDEO = 0x28,   // 低光视频（超级夜景）
    CAMERA_MODE_SMART_TRACKING = 0x34     // 智能跟随
} camera_mode_t;

typedef enum {
    CREATE_MODE_CJSON = 0,  // 通过 cJSON
    CREATE_MODE_STRUCT = 1  // 通过结构体
} create_mode_t;

#endif
