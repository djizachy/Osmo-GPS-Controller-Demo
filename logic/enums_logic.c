#include "enums_logic.h"

const char* camera_mode_to_string(camera_mode_t mode) {
    switch (mode) {
        case CAMERA_MODE_SLOW_MOTION:
            return "慢动作模式";
        case CAMERA_MODE_NORMAL:
            return "普通模式";
        case CAMERA_MODE_TIMELAPSE_STATIC:
            return "延时摄影模式 1 静止延时";
        case CAMERA_MODE_PHOTO:
            return "普通拍照模式";
        case CAMERA_MODE_TIMELAPSE_MOTION:
            return "延时摄影模式 2 运动延时";
        case CAMERA_MODE_LIVE_STREAMING:
            return "直播模式";
        case CAMERA_MODE_UVC_STREAMING:
            return "UVC 直播模式";
        case CAMERA_MODE_LOW_LIGHT_VIDEO:
            return "低光视频（超级夜景）";
        case CAMERA_MODE_SMART_TRACKING:
            return "智能跟随";
        default:
            return "未知模式";
    }
}

const char* camera_status_to_string(camera_status_t status) {
    switch (status) {
        case CAMERA_STATUS_SCREEN_OFF:
            return "屏幕关闭";
        case CAMERA_STATUS_LIVE_STREAMING:
            return "直播";
        case CAMERA_STATUS_PLAYBACK:
            return "回放";
        case CAMERA_STATUS_PHOTO_OR_RECORDING:
            return "拍照或录像中";
        case CAMERA_STATUS_PRE_RECORDING:
            return "预录制中";
        default:
            return "未知状态";
    }
}

const char* video_resolution_to_string(video_resolution_t res) {
    switch (res) {
        case VIDEO_RESOLUTION_1080P: return "1920x1080P";
        case VIDEO_RESOLUTION_2K_16_9: return "2720x1530P 2.7K 16:9";
        case VIDEO_RESOLUTION_2K_4_3: return "2720x2040P 2.7K 4:3";
        case VIDEO_RESOLUTION_4K_16_9: return "4096x2160P 4K 16:9";
        case VIDEO_RESOLUTION_4K_4_3: return "4096x3072P 4K 4:3";
        default: return "未知分辨率";
    }
}

const char* fps_idx_to_string(fps_idx_t fps) {
    switch (fps) {
        case FPS_24: return "24fps";
        case FPS_25: return "25fps";
        case FPS_30: return "30fps";
        case FPS_48: return "48fps";
        case FPS_50: return "50fps";
        case FPS_60: return "60fps";
        case FPS_100: return "100fps";
        case FPS_120: return "120fps";
        case FPS_200: return "200fps";
        case FPS_240: return "240fps";
        default: return "未知帧率";
    }
}

const char* eis_mode_to_string(eis_mode_t mode) {
    switch (mode) {
        case EIS_MODE_OFF: return "关闭";
        case EIS_MODE_RS: return "RS";
        case EIS_MODE_RS_PLUS: return "RS+";
        case EIS_MODE_HB: return "-HB";
        case EIS_MODE_HS: return "-HS";
        default: return "未知防抖模式";
    }
}
