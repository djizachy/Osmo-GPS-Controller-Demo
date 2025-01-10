#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"
#include "dji_protocol_data_processor.h"

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

const structure_descriptor_t *find_descriptor_by_structure(uint8_t cmd_set, uint8_t cmd_id) {
    for (size_t i = 0; i < STRUCTURE_DESCRIPTORS_COUNT; ++i) {
        if (structure_descriptors[i].cmd_set == cmd_set && structure_descriptors[i].cmd_id == cmd_id) {
            return &structure_descriptors[i];
        }
    }
    return NULL;
}

/**
 * @brief 解析单个字段的值，并将其存储在 JSON 对象中。
 * 
 * 该函数根据字段描述信息解析指定数据段，并将解析出的字段值添加到 JSON 对象中。如果字段包含不可打印字符，则将其转换为十六进制字符串。动态长度字段的处理方式为：如果它是最后一个字段，则解析剩余的数据；否则，动态字段不允许出现中间位置。
 * 
 * @param data 当前字段数据段的指针。
 * @param data_length 当前字段数据段的剩余长度。
 * @param field 字段描述信息，包含字段的名称、大小、是否为必填等信息。
 * @param field_index 当前字段的索引，用于确定动态长度字段的位置。
 * @param field_count 总字段数量，用于检查动态长度字段是否在最后。
 * @param output cJSON 对象，用于存储解析后的字段值。
 * @param offset 解析过程中更新的数据偏移量，表示当前已处理的数据长度。
 * @return 0 表示成功，非 0 表示失败。
 * 
 * @note 如果解析失败，函数会在日志中输出错误信息，并返回非零值。
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
 * @param cmd_type 命令类型。
 * @param data 数据段指针。
 * @param data_length 数据段长度。
 * @param output 输出的 cJSON 对象，用于存储解析的字段值。
 * @return 0 表示成功，非 0 表示失败。
 */
int data_parser(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const uint8_t *data, size_t data_length, cJSON *output) {
    ESP_LOGI(TAG, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X", cmd_set, cmd_id, cmd_type);

    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        ESP_LOGE(TAG, "No descriptor found for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return -1;
    }

    // 根据 cmd_type 判断是命令帧还是应答帧，选择对应字段解析
    if ((cmd_type & 0x20) == 0) { // 第5位为0：命令帧
        if (descriptor->command_data_fields == NULL || descriptor->command_data_field_count == 0) {
            ESP_LOGE(TAG, "No command data fields defined for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
            return -1;
        }
        return parse_fields(data, data_length, descriptor->command_data_field_count, descriptor->command_data_fields, output);
    } else { // 第5位为1：应答帧
        if (descriptor->response_data_fields == NULL || descriptor->response_data_field_count == 0) {
            ESP_LOGE(TAG, "No response data fields defined for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
            return -1;
        }
        return parse_fields(data, data_length, descriptor->response_data_field_count, descriptor->response_data_fields, output);
    }
}

int data_parser_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const uint8_t *data, size_t data_length, cJSON *output) {
    fprintf(stderr, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X\n", cmd_set, cmd_id, cmd_type);

    // 查找对应的命令描述符
    const structure_descriptor_t *descriptor = find_descriptor_by_structure(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    // 检查 parser 函数是否存在
    if (descriptor->parser == NULL) {
        fprintf(stderr, "Parser function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    // 调用 parser 函数
    return descriptor->parser(data, data_length, output, cmd_type);
}

// 工具函数
void hex_string_to_bytes(const char *hex_str, uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        sscanf(&hex_str[i * 2], "%2hhX", &data[i]);  // 每两位字符转为一个字节
    }
}

/**
 * @brief 根据 CmdSet 和 CmdID，构造数据段。
 * 
 * 根据命令集标识符（CmdSet）和命令标识符（CmdID），
 * 以及输入的 cJSON 对象，构造一个数据段。
 * 
 * @param cmd_set CmdSet 标识符。
 * @param cmd_id CmdID 标识符。
 * @param cmd_type 命令类型。
 * @param key_values cJSON 对象，包含字段名称和值。
 * @param key_value_count key-value 数组的大小。
 * @param data_length 输出数据段的长度（不包括 CmdSet 和 CmdID）。
 * @return 返回构造好的数据段指针，如果内存分配失败则返回 NULL。
 */
uint8_t* data_creator(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const cJSON *key_values, size_t *data_length) {
    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        ESP_LOGE(TAG, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return NULL;
    }

    // 根据 cmd_type 判断使用 command_data_fields 或 response_data_fields
    const data_field_t *data_fields = NULL;
    size_t field_count = 0;

    if ((cmd_type & 0x20) == 0) { // 第5位为0：命令帧
        data_fields = descriptor->command_data_fields;
        field_count = descriptor->command_data_field_count;
    } else { // 第5位为1：应答帧
        data_fields = descriptor->response_data_fields;
        field_count = descriptor->response_data_field_count;
    }

    if (data_fields == NULL || field_count == 0) {
        ESP_LOGW(TAG, "No data fields defined for CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X", cmd_set, cmd_id, cmd_type);
        return NULL;
    }

    // 计算数据段长度（不包括 CmdSet 和 CmdID）
    *data_length = 0;
    for (size_t i = 0; i < field_count; ++i) {
        *data_length += data_fields[i].size;
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
        const char *hex_str = cJSON_GetStringValue(item);  // 获取十六进制字符串

        // 打印 key 和对应的十六进制字符串
        if (hex_str) {
            printf("Key: %s\n", key);
            printf("Field Data (Hex): %s\n", hex_str);

            // 查找字段的大小
            size_t field_size = 0;
            for (size_t i = 0; i < field_count; ++i) {
                if (strcmp(data_fields[i].field_name, key) == 0) {
                    field_size = data_fields[i].size;
                    break;
                }
            }

            if (field_size > 0) {
                uint8_t field_data[256];  // 假设最大字节数为 256
                hex_string_to_bytes(hex_str, field_data, field_size);  // 转换为字节数据

                // 打印解析后的字节数据
                printf("Field Data (Bytes): ");
                for (size_t i = 0; i < field_size; ++i) {
                    printf("0x%02X ", field_data[i]);
                }
                printf("\n");

                // 将字节数据复制到 data 数组
                memcpy(data + offset, field_data, field_size);
                offset += field_size;
            }
        }
        item = item->next;
    }

    return data;
}

uint8_t* data_creator_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, size_t *data_length) {
    // 查找对应的命令描述符
    const structure_descriptor_t *descriptor = find_descriptor_by_structure(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    // 检查 creator 函数是否存在
    if (descriptor->creator == NULL) {
        fprintf(stderr, "Creator function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    // 调用 creator 函数
    return descriptor->creator(structure, data_length, cmd_type);
}