#include "dji_protocol_data_structures.h"

/* 切换模式 */
const data_field_t MODE_SWITCH_CM_DF[] = {
    {"device_id", 0, 4, true},   /**< 设备 ID，长度 4 字节，必填 */
    {"mode", 4, 1, true},        /**< 模式，长度 1 字节，必填 */
    {"reserved", 5, 4, false}    /**< 预留字段，长度 4 字节，非必填 */
};
const data_field_t MODE_SWITCH_RE_DF[] = {
    {"ret_code", 0, 1, true},    /**< 返回码，长度 1 字节，必填 */
    {"reserved", 1, 4, false}    /**< 预留字段，长度 4 字节，非必填 */
};

/* 版本号查询 */
const data_field_t GET_VERSION_CM_DF[] = {
    {"", 0, 0, false}            /**< 空字段（占位符） */
};
const data_field_t GET_VERSION_RE_DF[] = {
    {"ack_result", 0, 2, true},    /**< 应答结果，长度 2 字节，必填 */
    {"product_id", 2, 16, true},   /**< 产品 ID，长度 16 字节，例如 DJI-RS3，必填 */
    {"sdk_version", 18, -1, true}  /**< SDK 版本，动态长度字段，用 -1 表示，必填 */
};

/* 拍录控制 */
const data_field_t RECORD_CONTROL_CM_DF[] = {
    {"device_id", 0, 4, true},     /**< 设备 ID，长度 4 字节，必填 */
    {"record_ctrl", 4, 1, true},   /**< 拍录控制，0 表示开始录制，1 表示停止录制 */
    {"reserved", 5, 4, false}      /**< 预留字段，长度 4 字节，非必填 */
};
const data_field_t RECORD_CONTROL_RE_DF[] = {
    {"ret_code", 0, 1, true},      /**< 返回码，参考普通返回码 */
    {"reserved", 1, 4, false}      /**< 预留字段，长度 4 字节，非必填 */
};