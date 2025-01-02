#include "dji_protocol_data_descriptors.h"

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
    {0x1D, 0x03, (data_field_t *)RECORD_CONTROL_CM_DF, 3, (data_field_t *)RECORD_CONTROL_RE_DF, 2},
};

const size_t DESCRIPTORS_COUNT = sizeof(data_descriptors) / sizeof(data_descriptors[0]);
