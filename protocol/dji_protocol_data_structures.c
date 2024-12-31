#include "dji_protocol_data_structures.h"

/* 切换模式 */
const data_field_t MODE_SWITCH_COMMAND_DATA_FIELDS[] = {
    {"device_id", 0, 4, true},   /**< 设备 ID 字段 */
    {"mode", 4, 1, true},        /**< 模式字段 */
    {"reserved", 5, 4, false}    /**< 预留字段 */
};
const data_field_t MODE_SWITCH_RESPONSE_DATA_FIELDS[] = {
    {"ret_code", 0, 1, true},    /**< 返回码字段 */
    {"reserved", 1, 4, false}    /**< 预留字段 */
};

/* 版本号查询 */
const data_field_t GET_VERSION_COMMAND_DATA_FIELDS[] = {
    {"", 0, 0, false},
};
const data_field_t GET_VERSION_RESPONSE_DATA_FIELDS[] = {
    {"ack_result", 0, 2, true},    // 应答结果
    {"product_id", 2, 16, true},   // 产品 ID，如 DJI-RS3
    {"sdk_version", 18, -1, true}  // 动态长度字段，用 -1 标识
};