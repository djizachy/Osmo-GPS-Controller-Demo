#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "dji_protocol_data_descriptors.h"

#define TAG "DJI_PROTOCOL_DATA_DESCRIPTORS"

/**
 * @brief 数据描述符数组，包含命令集、命令标识符、命令帧与应答帧的字段布局。
 * 
 * 该数组用于存储不同命令集（CmdSet）和命令标识符（CmdID）对应的命令帧与应答帧的字段描述信息。
 * 每个元素描述了命令帧与应答帧的字段布局，包括字段的指针和数量。
 * 
 * @note 本例中定义了一个命令集和命令 ID 的数据描述符，具体应用可以根据需求扩展。
 */
const data_descriptor_t data_descriptors[] = {
    { 
        0x1D,                                   // CmdSet: 命令集标识符
        0x04,                                   // CmdID: 命令标识符
        (data_field_t *)MODE_SWITCH_CM_DF,      // 命令帧字段指针
        3,                                      // 命令帧字段数量
        (data_field_t *)MODE_SWITCH_RE_DF,      // 应答帧字段指针
        2                                       // 应答帧字段数量
    },
    {0x00, 0x00, (data_field_t *)GET_VERSION_CM_DF, 0, (data_field_t *)GET_VERSION_RE_DF, 3},
    {0x1D, 0x03, (data_field_t *)RECORD_CONTROL_CM_DF, 3, (data_field_t *)RECORD_CONTROL_RE_DF, 2}
};

const size_t DESCRIPTORS_COUNT = sizeof(data_descriptors) / sizeof(data_descriptors[0]);

/* 结构体支持 */
const structure_descriptor_t structure_descriptors[] = {
    { 
        0x00, 0x17,
        (data_creator_func_t)gps_data_creator, 
        (data_parser_func_t)gps_data_parser
    }
    // 可以在这里添加更多结构体对应的描述符
};

// 数据描述符数组的大小
const size_t STRUCTURE_DESCRIPTORS_COUNT = sizeof(structure_descriptors) / sizeof(structure_descriptors[0]);

// 下面定义结构体支持的 creator 和 parser

/**
 * @brief 根据 GPS 命令帧结构体创建数据段。
 * 
 * @param gps_frame GPS 命令帧结构体指针。
 * @param data_length 输出数据段的长度（不包括 CmdSet 和 CmdID）。
 * @return 返回构造好的数据段指针，如果内存分配失败则返回 NULL。
 */
uint8_t* gps_data_creator(const void *structure, size_t *data_length) {
    const gps_data_push_command_frame *gps_frame = (const gps_data_push_command_frame *)structure;

    // 计算数据段长度
    *data_length = sizeof(gps_data_push_command_frame);

    // 日志：记录数据段长度
    ESP_LOGI(TAG, "Data length calculated for gps_data_push_command_frame: %zu", *data_length);

    // 分配内存
    uint8_t *data = (uint8_t *)malloc(*data_length);
    if (data == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed in gps_data_creator");
        return NULL;
    }

    // 日志：内存分配成功
    ESP_LOGI(TAG, "Memory allocation succeeded, copying data...");

    // 填充数据段
    memcpy(data, gps_frame, *data_length);

    return data;
}

int gps_data_parser(const uint8_t *data, size_t data_length, cJSON *output) {
    if (data == NULL || output == NULL) {
        ESP_LOGE(TAG, "gps_data_parser: NULL input detected");
        return -1;  // 返回错误码
    }

    // 日志：记录传入的数据长度
    ESP_LOGI(TAG, "Parsing GPS data, received data length: %zu", data_length);

    // 检查数据长度是否足够
    if (data_length < sizeof(gps_data_push_response_frame)) {
        ESP_LOGE(TAG, "gps_data_parser: Data length too short. Expected: %zu, Got: %zu",
                 sizeof(gps_data_push_response_frame), data_length);
        return -1;  // 返回错误码
    }

    // 转换为 GPS 响应帧结构体
    const gps_data_push_response_frame *response = (const gps_data_push_response_frame *)data;

    // 将解析结果填充到 cJSON 对象中
    cJSON_AddNumberToObject(output, "ret_code", response->ret_code);

    // 日志：打印解析结果
    ESP_LOGI(TAG, "Parsed GPS Data Push Response Frame:");
    ESP_LOGI(TAG, "  Ret Code: 0x%02X", response->ret_code);

    return 0;  // 返回成功
}
