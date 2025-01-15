#ifndef __ENUMS_LOGIC_H__
#define __ENUMS_LOGIC_H__

typedef enum {
    CREATE_MODE_CJSON = 0,  // 通过 cJSON
    CREATE_MODE_STRUCT = 1  // 通过结构体
} create_mode_t;

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
const char* camera_mode_to_string(camera_mode_t mode);

typedef enum {
    CAMERA_STATUS_SCREEN_OFF = 0x00,          // 屏幕关闭
    CAMERA_STATUS_LIVE_STREAMING = 0x01,      // 直播
    CAMERA_STATUS_PLAYBACK = 0x02,            // 回放
    CAMERA_STATUS_PHOTO_OR_RECORDING = 0x03,  // 拍照或录像中
    CAMERA_STATUS_PRE_RECORDING = 0x05        // 预录制中
} camera_status_t;
const char* camera_status_to_string(camera_status_t status);

typedef enum {
    VIDEO_RESOLUTION_1080P = 10,         // 1920x1080P
    VIDEO_RESOLUTION_2K_16_9 = 45,       // 2720x1530P 2.7K 16:9
    VIDEO_RESOLUTION_2K_4_3 = 95,        // 2720x2040P 2.7K 4:3
    VIDEO_RESOLUTION_4K_16_9 = 16,       // 4096x2160P 4K 16:9
    VIDEO_RESOLUTION_4K_4_3 = 103        // 4096x3072P 4K 4:3
} video_resolution_t;
const char* video_resolution_to_string(video_resolution_t res);

typedef enum {
    FPS_24 = 1,     // 24fps
    FPS_25 = 2,     // 25fps
    FPS_30 = 3,     // 30fps
    FPS_48 = 4,     // 48fps
    FPS_50 = 5,     // 50fps
    FPS_60 = 6,     // 60fps
    FPS_100 = 10,   // 100fps
    FPS_120 = 7,    // 120fps
    FPS_200 = 19,   // 200fps
    FPS_240 = 8     // 240fps
} fps_idx_t;
const char* fps_idx_to_string(fps_idx_t fps);

typedef enum {
    EIS_MODE_OFF = 0,      // 关闭
    EIS_MODE_RS = 1,       // RS
    EIS_MODE_RS_PLUS = 2,  // RS+
    EIS_MODE_HB = 3,       // -HB
    EIS_MODE_HS = 4        // -HS
} eis_mode_t;
const char* eis_mode_to_string(eis_mode_t mode);

#endif
