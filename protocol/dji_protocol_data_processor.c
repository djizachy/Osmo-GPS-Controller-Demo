/*
 * Copyright (c) 2025 DJI
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"

#include "dji_protocol_data_processor.h"

#define TAG "DJI_PROTOCOL_DATA_PROCESSOR"

/**
 * @brief Find data descriptor by command set and command ID
 *        根据命令集和命令ID查找对应的数据描述符
 * 
 * @param cmd_set Command set
 *                命令集
 * @param cmd_id Command ID
 *               命令ID
 * @return Return pointer to found data descriptor, NULL if not found
 *         返回找到的数据描述符指针，如果未找到则返回NULL
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
 * @brief Parse data according to structure
 *        根据结构体解析数据
 * 
 * @param cmd_set Command set
 *                命令集
 * @param cmd_id Command ID
 *               命令ID
 * @param cmd_type Command type
 *                 命令类型
 * @param data Data to be parsed
 *             待解析的数据
 * @param data_length Data length
 *                    数据长度
 * @param structure_out Output structure pointer
 *                      输出结构体指针
 * @return Return 0 on success, -1 on failure
 *         成功返回0，失败返回-1
 */
int data_parser_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const uint8_t *data, size_t data_length, void *structure_out) {
    ESP_LOGI(TAG, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X", cmd_set, cmd_id, cmd_type);

    // Find corresponding descriptor
    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    // Check if parser function exists
    // 检查解析函数是否存在
    if (descriptor->parser == NULL) {
        fprintf(stderr, "Parser function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    return descriptor->parser(data, data_length, structure_out, cmd_type);
}

/**
 * @brief Create data according to structure
 *        根据结构体创建数据
 * 
 * @param cmd_set Command set
 *                命令集
 * @param cmd_id Command ID
 *               命令ID
 * @param cmd_type Command type
 *                 命令类型
 * @param structure Input structure pointer
 *                  输入结构体指针
 * @param data_length Output data length
 *                    输出数据长度
 * @return Return pointer to created data buffer, NULL on failure
 *         返回创建的数据缓冲区指针，失败返回NULL
 */
uint8_t* data_creator_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, size_t *data_length) {
    // Find corresponding descriptor
    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    // Check if creator function exists
    // 检查创建函数是否存在
    if (descriptor->creator == NULL) {
        fprintf(stderr, "Creator function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    return descriptor->creator(structure, data_length, cmd_type);
}
