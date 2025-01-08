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
    {0x00, 0x17, (data_creator_func_t)gps_data_creator, (data_parser_func_t)gps_data_parser},
    {0x00, 0x19, (data_creator_func_t)connection_data_creator, (data_parser_func_t)connection_data_parser}
};
const size_t STRUCTURE_DESCRIPTORS_COUNT = sizeof(structure_descriptors) / sizeof(structure_descriptors[0]);

// 下面定义结构体支持的 creator 和 parser
uint8_t* gps_data_creator(const void *structure, size_t *data_length, uint8_t cmd_type) {
    if (structure == NULL || data_length == NULL) {
        ESP_LOGE(TAG, "Invalid input: structure or data_length is NULL");
        return NULL;
    }

    uint8_t *data = NULL;

    // 判断是否为命令帧 (cmd_type & 0x20 == 0)
    if ((cmd_type & 0x20) == 0) {
        // 命令帧
        const gps_data_push_command_frame *gps_command_frame = (const gps_data_push_command_frame *)structure;

        // 计算数据段长度
        *data_length = sizeof(gps_data_push_command_frame);

        // 日志：记录数据段长度
        ESP_LOGI(TAG, "Data length calculated for gps_data_push_command_frame: %zu", *data_length);

        // 分配内存
        data = (uint8_t *)malloc(*data_length);
        if (data == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed in gps_data_creator (command frame)");
            return NULL;
        }

        // 日志：内存分配成功
        ESP_LOGI(TAG, "Memory allocation succeeded for command frame, copying data...");

        // 填充数据段
        memcpy(data, gps_command_frame, *data_length);

    } else {
        // 应答帧
        const gps_data_push_response_frame *gps_response_frame = (const gps_data_push_response_frame *)structure;

        // 计算数据段长度
        *data_length = sizeof(gps_data_push_response_frame);

        // 日志：记录数据段长度
        ESP_LOGI(TAG, "Data length calculated for gps_data_push_response_frame: %zu", *data_length);

        // 分配内存
        data = (uint8_t *)malloc(*data_length);
        if (data == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed in gps_data_creator (response frame)");
            return NULL;
        }

        // 日志：内存分配成功
        ESP_LOGI(TAG, "Memory allocation succeeded for response frame, copying data...");

        // 填充数据段
        memcpy(data, gps_response_frame, *data_length);
    }

    // 返回数据段指针
    return data;
}

int gps_data_parser(const uint8_t *data, size_t data_length, cJSON *output, uint8_t cmd_type) {
    if (data == NULL || output == NULL) {
        ESP_LOGE(TAG, "gps_data_parser: NULL input detected");
        return -1;  // 返回错误码
    }

    // 日志：记录传入的数据长度
    ESP_LOGI(TAG, "Parsing GPS data, received data length: %zu", data_length);

    // 判断是否为命令帧 (cmd_type & 0x20 == 0)
    if ((cmd_type & 0x20) == 0) {
        ESP_LOGW(TAG, "gps_data_parser: Parsing command frame is not supported.");
        return -1;  // 返回错误码，表示暂不支持命令帧
    }

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

    return 0;  // 返回成功
}

uint8_t* connection_data_creator(const void *structure, size_t *data_length, uint8_t cmd_type) {
    if (structure == NULL || data_length == NULL) {
        ESP_LOGE(TAG, "connection_request_data_creator: NULL input detected");
        return NULL;
    }

    uint8_t *data = NULL;

    // 判断是否为命令帧 (cmd_type & 0x20 == 0)
    if ((cmd_type & 0x20) == 0) {
        // 命令帧
        const connection_request_command_frame *command_frame = (const connection_request_command_frame *)structure;

        // 计算数据段长度
        *data_length = sizeof(connection_request_command_frame);

        // 日志：记录数据段长度
        ESP_LOGI(TAG, "Data length calculated for connection_request_command_frame: %zu", *data_length);

        // 分配内存
        data = (uint8_t *)malloc(*data_length);
        if (data == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed in connection_request_data_creator (command frame)");
            return NULL;
        }

        // 填充数据段
        memcpy(data, command_frame, *data_length);

    } else {
        // 应答帧
        const connection_request_response_frame *response_frame = (const connection_request_response_frame *)structure;

        // 计算数据段长度
        *data_length = sizeof(connection_request_response_frame);

        // 日志：记录数据段长度
        ESP_LOGI(TAG, "Data length calculated for connection_request_response_frame: %zu", *data_length);

        // 分配内存
        data = (uint8_t *)malloc(*data_length);
        if (data == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed in connection_request_data_creator (response frame)");
            return NULL;
        }

        // 填充数据段
        memcpy(data, response_frame, *data_length);
    }

    return data;
}

int connection_data_parser(const uint8_t *data, size_t data_length, cJSON *output, uint8_t cmd_type) {
    if (data == NULL || output == NULL) {
        ESP_LOGE(TAG, "connection_request_data_parser: NULL input detected");
        return -1;  // 返回错误码
    }

    // 日志：记录传入的数据长度
    ESP_LOGI(TAG, "Parsing Connection Request data, received data length: %zu", data_length);

    // 判断是否为命令帧 (cmd_type & 0x20 == 0) 或响应帧 (cmd_type & 0x20 != 0)
    if ((cmd_type & 0x20) == 0) {
        // 解析命令帧
        ESP_LOGI(TAG, "Parsing command frame...");

        // 检查数据长度是否足够
        if (data_length < sizeof(connection_request_command_frame)) {
            ESP_LOGE(TAG, "connection_request_data_parser: Data length too short for command frame. Expected: %zu, Got: %zu",
                     sizeof(connection_request_command_frame), data_length);
            return -1;  // 返回错误码
        }

        // 转换为连接请求命令帧结构体
        const connection_request_command_frame *command = (const connection_request_command_frame *)data;

        // 填充 cJSON 对象
        cJSON_AddNumberToObject(output, "device_id", command->device_id);
        cJSON_AddNumberToObject(output, "mac_addr_len", command->mac_addr_len);

        // 填充 MAC 地址
        cJSON *mac_array = cJSON_CreateArray();
        for (size_t i = 0; i < command->mac_addr_len && i < sizeof(command->mac_addr); ++i) {
            cJSON_AddItemToArray(mac_array, cJSON_CreateNumber(command->mac_addr[i]));
        }
        cJSON_AddItemToObject(output, "mac_addr", mac_array);

        cJSON_AddNumberToObject(output, "fw_version", command->fw_version);
        cJSON_AddNumberToObject(output, "conidx", command->conidx);
        cJSON_AddNumberToObject(output, "verify_mode", command->verify_mode);
        cJSON_AddNumberToObject(output, "verify_data", command->verify_data);

        // 填充 reserved 字段
        cJSON *reserved_array = cJSON_CreateArray();
        for (size_t i = 0; i < sizeof(command->reserved); ++i) {
            cJSON_AddItemToArray(reserved_array, cJSON_CreateNumber(command->reserved[i]));
        }
        cJSON_AddItemToObject(output, "reserved", reserved_array);

        return 0;  // 返回成功
    } else {
        // 解析响应帧
        ESP_LOGI(TAG, "Parsing response frame...");

        // 检查数据长度是否足够
        if (data_length < sizeof(connection_request_response_frame)) {
            ESP_LOGE(TAG, "connection_request_data_parser: Data length too short for response frame. Expected: %zu, Got: %zu",
                     sizeof(connection_request_response_frame), data_length);
            return -1;  // 返回错误码
        }

        // 转换为连接请求响应帧结构体
        const connection_request_response_frame *response = (const connection_request_response_frame *)data;

        // 填充 cJSON 对象
        cJSON_AddNumberToObject(output, "device_id", response->device_id);
        cJSON_AddNumberToObject(output, "ret_code", response->ret_code);

        // 填充 reserved 字段
        cJSON *reserved_array = cJSON_CreateArray();
        for (size_t i = 0; i < sizeof(response->reserved); ++i) {
            cJSON_AddItemToArray(reserved_array, cJSON_CreateNumber(response->reserved[i]));
        }
        cJSON_AddItemToObject(output, "reserved", reserved_array);

        return 0;  // 返回成功
    }
}