#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "dji_protocol_data_processor.h"
#include "cJSON.h"

#define TAG "DJI_PROTOCOL_DATA_PROCESSOR"  // 设置日志标签，用于日志输出标识

/**
 * @brief 根据 CmdSet 和 CmdID 查找对应的命令描述符。
 * 
 * 根据输入的命令集标识符（CmdSet）和命令标识符（CmdID），
 * 在全局数据描述符数组中查找匹配的命令描述符。
 * 
 * @param cmd_set 命令集标识符。
 * @param cmd_id 命令标识符。
 * @return 返回找到的命令描述符指针，如果没有找到则返回 NULL。
 */
const data_descriptor_t *find_descriptor(uint8_t cmd_set, uint8_t cmd_id) {
    for (size_t i = 0; i < DESCRIPTORS_COUNT; ++i) {
        // 如果找到匹配的命令描述符，则返回该描述符指针
        if (data_descriptors[i].cmd_set == cmd_set && data_descriptors[i].cmd_id == cmd_id) {
            return &data_descriptors[i];
        }
    }
    // 如果没有找到，返回 NULL
    return NULL;
}

/**
 * @brief 解析单个字段。
 * 
 * @param data 当前字段数据段的指针。
 * @param data_length 当前字段数据段的剩余长度。
 * @param field 字段描述信息。
 * @param output cJSON 对象，用于存储解析的字段值。
 * @param offset 解析过程中更新的数据偏移量。
 * @return 0 表示成功，非 0 表示失败。
 */
static int parse_single_field(const uint8_t *data, size_t data_length, const data_field_t *field, size_t field_index, size_t field_count, cJSON *output, size_t *offset) {
    size_t field_size = field->size;

    // 动态长度字段处理
    if (field_size == (size_t)-1) {
        // 检查是否为最后一个字段
        if (field_index != field_count - 1) {
            ESP_LOGE(TAG, "Dynamic length field %s is not the last field in the descriptor", field->field_name);
            return -1; // 配置错误，中间不允许动态长度字段
        }

        // 如果是最后一个字段，解析剩余所有数据
        field_size = data_length - *offset;
        if (field_size == 0) {
            ESP_LOGE(TAG, "Dynamic length field %s: no data remaining to parse", field->field_name);
            return -1;
        }
    }


    // 检查剩余数据长度
    if (*offset + field_size > data_length) {
        ESP_LOGW(TAG, "Field %s: insufficient data (expected: %zu, available: %zu)", field->field_name, field_size, data_length - *offset);
        if (field->is_required) {
            ESP_LOGE(TAG, "Required field %s is missing!", field->field_name);
            return -1;
        }
        return 0; // 可选字段，跳过
    }

    // // 提取字段值
    // char *value_str = (char *)malloc(field_size + 1);
    // if (value_str == NULL) {
    //     ESP_LOGE(TAG, "Failed to allocate memory for field: %s", field->field_name);
    //     return -1;
    // }
    // memcpy(value_str, data + *offset, field_size);
    // value_str[field_size] = '\0'; // 确保字符串以 NULL 结尾

    // // 添加字段到 JSON
    // if (!cJSON_AddStringToObject(output, field->field_name, value_str)) {
    //     ESP_LOGE(TAG, "Failed to add field %s to JSON object", field->field_name);
    //     free(value_str);
    //     return -1;
    // }

    // free(value_str);

    // ESP_LOGI(TAG, "Parsed field: %s", field->field_name);

    // *offset += field_size; // 更新偏移量
    // return 0;

    // 分配内存存储字段值
    char *value_str = (char *)malloc(field_size + 1);
    if (value_str == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for field: %s", field->field_name);
        return -1;
    }
    memcpy(value_str, data + *offset, field_size);
    value_str[field_size] = '\0'; // 确保字符串以 NULL 结尾

    // 检查是否为有效的字符串（只包含可打印字符）
    int is_printable = 1;
    for (size_t i = 0; i < field_size; i++) {
        if (data[*offset + i] < 32 || data[*offset + i] > 126) { // 非可打印字符范围
            is_printable = 0;
            break;
        }
    }

    // 根据内容添加到 JSON
    if (is_printable) {
        // 可解析为字符串
        if (!cJSON_AddStringToObject(output, field->field_name, value_str)) {
            ESP_LOGE(TAG, "Failed to add field %s to JSON object", field->field_name);
            free(value_str);
            return -1;
        }
    } else {
        // 不可解析为字符串，转为十六进制字符串
        char *hex_value = (char *)malloc(field_size * 2 + 1); // 每字节 2 个十六进制字符
        if (hex_value == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for binary field: %s", field->field_name);
            free(value_str);
            return -1;
        }

        for (size_t i = 0; i < field_size; i++) {
            sprintf(hex_value + i * 2, "%02X", data[*offset + i]);
        }
        hex_value[field_size * 2] = '\0'; // 确保以 NULL 结尾

        if (!cJSON_AddStringToObject(output, field->field_name, hex_value)) {
            ESP_LOGE(TAG, "Failed to add binary field %s to JSON object", field->field_name);
            free(value_str);
            free(hex_value);
            return -1;
        }

        free(hex_value);
    }

    free(value_str);

    ESP_LOGI(TAG, "Parsed field: %s", field->field_name);

    *offset += field_size; // 更新偏移量
    return 0;
}

/**
 * @brief 根据字段描述解析数据。
 * 
 * @param data 数据段。
 * @param data_length 数据段的实际长度。
 * @param field_count 字段数量。
 * @param fields 字段描述数组，描述了每个字段的信息。
 * @param output 输出的 cJSON 对象，用于存储解析的字段值。
 * @return 0 表示成功，非 0 表示失败。
 */
int parse_fields(const uint8_t *data, size_t data_length, size_t field_count, const data_field_t *fields, cJSON *output) {
    size_t offset = 0;

    for (size_t i = 0; i < field_count; ++i) {
        if (parse_single_field(data, data_length, &fields[i], i, field_count, output, &offset) != 0) {
            return -1; // 单个字段解析失败
        }
    }

    return 0;
}

/**
 * @brief 根据 CmdSet 和 CmdID，解析数据段中的字段。
 * 
 * @param cmd_set CmdSet 字段。
 * @param cmd_id CmdID 字段。
 * @param data 数据段指针。
 * @param data_length 数据段长度。
 * @param output 输出的 cJSON 对象，用于存储解析的字段值。
 * @return 0 表示成功，非 0 表示失败。
 */
int data_parser(uint8_t cmd_set, uint8_t cmd_id, const uint8_t *data, size_t data_length, cJSON *output) {
    ESP_LOGI(TAG, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);

    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        ESP_LOGE(TAG, "No descriptor found for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return -1;
    }

    // 解析字段
    return parse_fields(data, data_length, descriptor->response_data_field_count, descriptor->response_data_fields, output);
}

/**
 * @brief 根据 CmdSet 和 CmdID，构造数据段。
 * 
 * 根据命令集标识符（CmdSet）和命令标识符（CmdID），
 * 以及输入的 cJSON 对象，构造一个数据段。
 * 
 * @param cmd_set CmdSet 标识符。
 * @param cmd_id CmdID 标识符。
 * @param key_values cJSON 对象，包含字段名称和值。
 * @param key_value_count key-value 数组的大小。
 * @param data_length 输出数据段的长度（不包括 CmdSet 和 CmdID）。
 * @return 返回构造好的数据段指针，如果内存分配失败则返回 NULL。
 */
uint8_t* data_creator(uint8_t cmd_set, uint8_t cmd_id, const cJSON *key_values, size_t *data_length) {
    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        ESP_LOGE(TAG, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return NULL;
    }

    // 计算数据段长度（不包括 CmdSet 和 CmdID）
    *data_length = 0;
    for (size_t i = 0; i < descriptor->command_data_field_count; ++i) {
        *data_length += descriptor->command_data_fields[i].size;
    }

    // 如果数据段长度为 0，直接返回一个空指针
    if (*data_length == 0) {
        ESP_LOGW(TAG, "Data length is zero for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return NULL; // 或者返回一个特定值表示空数据段
    }

    // 动态分配内存
    uint8_t *data = (uint8_t *)malloc(*data_length);
    if (data == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for data_creator");
        return NULL;
    }

    // 填充数据段
    size_t offset = 0;
    cJSON *item = key_values->child;
    while (item) {
        const char *key = item->string;
        const uint8_t *field_data = (uint8_t *)cJSON_GetStringValue(item);
        size_t field_size = 0;
        for (size_t i = 0; i < descriptor->command_data_field_count; ++i) {
            if (strcmp(descriptor->command_data_fields[i].field_name, key) == 0) {
                field_size = descriptor->command_data_fields[i].size;
                break;
            }
        }
        if (field_size > 0) {
            memcpy(data + offset, field_data, field_size);
            offset += field_size;
        }

        item = item->next;
    }

    return data;
}
