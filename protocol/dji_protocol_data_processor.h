#ifndef DJI_PROTOCOL_DATA_PROCESSOR_H
#define DJI_PROTOCOL_DATA_PROCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"

#include "dji_protocol_data_descriptors.h"

/**
 * @brief 根据 CmdSet 和 CmdID 查找对应的命令描述符。
 * 
 * 在命令描述符数组中查找与指定 `cmd_set` 和 `cmd_id` 匹配的命令描述符。
 * 该函数用于根据协议描述符查找相应的数据结构。
 * 
 * @param cmd_set 命令集标识符。
 * @param cmd_id 命令标识符。
 * @return 返回找到的命令描述符指针，如果没有找到则返回 NULL。
 */
const data_descriptor_t *find_descriptor(uint8_t cmd_set, uint8_t cmd_id);
const structure_descriptor_t *find_descriptor_by_structure(uint8_t cmd_set, uint8_t cmd_id);

/**
 * @brief 根据 CmdSet 和 CmdID 解析数据。
 * 
 * 根据提供的 `cmd_set` 和 `cmd_id`，查找相关的命令描述符，并解析应答帧中的数据字段。
 * 
 * @param cmd_set CmdSet 字段值（命令集标识符）。
 * @param cmd_id CmdID 字段值（命令标识符）。
 * @param data 数据段指针，包含要解析的原始数据。
 * @param data_length 数据段的长度（字节数）。
 * @param output 输出的 cJSON 对象，用于存储解析后的字段名及其对应值。
 * @return 0 表示成功，非 0 表示错误。
 */
int data_parser(uint8_t cmd_set, uint8_t cmd_id, const uint8_t *data, size_t data_length, cJSON *output);

int data_parser_by_structure(uint8_t cmd_set, uint8_t cmd_id, const uint8_t *data, size_t data_length, cJSON *output);

/**
 * @brief 根据 CmdSet 和 CmdID 以及 key-value 数据，创建数据帧。
 * 
 * 根据指定的命令集标识符 (`cmd_set`)、命令标识符 (`cmd_id`) 和提供的 key-value 数据，
 * 生成一个数据帧。该函数会根据协议描述符来构建数据帧的具体格式。
 * 
 * 注意：生成的数据帧是动态分配的内存，调用方需要负责释放内存。
 * 
 * @param cmd_set CmdSet 字段值（命令集标识符）。
 * @param cmd_id CmdID 字段值（命令标识符）。
 * @param key_values 输入的 cJSON 对象，包含待封装的数据。
 * @param data_length 输出的帧总长度（不包括 CmdSet 和 CmdID 的长度）。
 * @return 动态分配的帧数据（需要外部释放），如果失败返回 NULL。
 */
uint8_t* data_creator(uint8_t cmd_set, uint8_t cmd_id, const cJSON *key_values, size_t *data_length);

// 测试
uint8_t* data_creator_by_structure(uint8_t cmd_set, uint8_t cmd_id, const void *structure, size_t *data_length);

#endif // DJI_PROTOCOL_DATA_PROCESSOR_H
