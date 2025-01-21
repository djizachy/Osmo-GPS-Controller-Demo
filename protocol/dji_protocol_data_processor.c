#include <string.h>
#include <stdio.h>
#include "esp_log.h"

#include "dji_protocol_data_processor.h"

#define TAG "DJI_PROTOCOL_DATA_PROCESSOR"

/**
 * @brief 根据命令集和命令ID查找对应的数据描述符
 * @param cmd_set 命令集
 * @param cmd_id 命令ID
 * @return 返回找到的数据描述符指针，如果未找到则返回NULL
 */
const data_descriptor_t *find_data_descriptor(uint8_t cmd_set, uint8_t cmd_id) {
    for (size_t i = 0; i < DATA_DESCRIPTORS_COUNT; ++i) {
        if (data_descriptors[i].cmd_set == cmd_set && data_descriptors[i].cmd_id == cmd_id) {
            return &data_descriptors[i];
        }
    }
    return NULL;
}

/**
 * @brief 根据结构体解析数据
 * @param cmd_set 命令集
 * @param cmd_id 命令ID
 * @param cmd_type 命令类型
 * @param data 待解析的数据
 * @param data_length 数据长度
 * @param structure_out 输出结构体指针
 * @return 成功返回0，失败返回-1
 */
int data_parser_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const uint8_t *data, size_t data_length, void *structure_out) {
    ESP_LOGI(TAG, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X", cmd_set, cmd_id, cmd_type);

    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    if (descriptor->parser == NULL) {
        fprintf(stderr, "Parser function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    return descriptor->parser(data, data_length, structure_out, cmd_type);
}

/**
 * @brief 根据结构体创建数据
 * @param cmd_set 命令集
 * @param cmd_id 命令ID
 * @param cmd_type 命令类型
 * @param structure 输入结构体指针
 * @param data_length 输出数据长度
 * @return 返回创建的数据缓冲区指针，失败返回NULL
 */
uint8_t* data_creator_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, size_t *data_length) {
    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    if (descriptor->creator == NULL) {
        fprintf(stderr, "Creator function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    return descriptor->creator(structure, data_length, cmd_type);
}
