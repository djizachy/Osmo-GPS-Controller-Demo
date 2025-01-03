#ifndef DJI_PROTOCOL_DATA_DESCRIPTORS_H
#define DJI_PROTOCOL_DATA_DESCRIPTORS_H

#include <stdint.h>
#include "cJSON.h"
#include "dji_protocol_data_structures.h"

/**
 * @brief 数据描述符结构体，用于描述指定命令集和命令 ID 对应的字段格式
 * 
 * 该结构体用于存储每个命令集和命令 ID 对应的命令帧和应答帧的字段布局信息，
 * 包括命令帧和应答帧的字段指针以及字段数量。
 */
typedef struct {
    uint8_t cmd_set;                      ///< 命令集标识符 (CmdSet)
    uint8_t cmd_id;                       ///< 命令标识符 (CmdID)
    data_field_t *command_data_fields;    ///< 命令帧字段指针
    size_t command_data_field_count;      ///< 命令帧字段的数量
    data_field_t *response_data_fields;   ///< 应答帧字段指针
    size_t response_data_field_count;     ///< 应答帧字段的数量
} data_descriptor_t;

// 数据描述符数组，用于存储所有命令集和命令 ID 对应的数据字段布局
extern const data_descriptor_t data_descriptors[];

// 数据描述符数组的大小，表示数组中元素的数量
extern const size_t DESCRIPTORS_COUNT;

/* 结构体支持 */
typedef uint8_t* (*data_creator_func_t)(const void *structure, size_t *data_length);
typedef int (*data_parser_func_t)(const uint8_t *data, size_t data_length, cJSON *output);

typedef struct {
    uint8_t cmd_set;                  ///< 命令集标识符 (CmdSet)
    uint8_t cmd_id;                   ///< 命令标识符 (CmdID)
    data_creator_func_t creator;      ///< 数据创建函数指针
    data_parser_func_t parser;        ///< 数据解析函数指针
} structure_descriptor_t;

extern const structure_descriptor_t structure_descriptors[];
extern const size_t STRUCTURE_DESCRIPTORS_COUNT;

uint8_t* gps_data_creator(const void *structure, size_t *data_length);
int gps_data_parser(const uint8_t *data, size_t data_length, cJSON *output);

#endif // DJI_PROTOCOL_DATA_DESCRIPTORS_H
