#ifndef DJI_PROTOCOL_DATA_DESCRIPTORS_H
#define DJI_PROTOCOL_DATA_DESCRIPTORS_H

#include <stdint.h>
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

#endif // DJI_PROTOCOL_DATA_DESCRIPTORS_H
