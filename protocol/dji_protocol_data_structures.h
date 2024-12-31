#ifndef DJI_PROTOCOL_DATA_STRUCTURES_H
#define DJI_PROTOCOL_DATA_STRUCTURES_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief 协议 DATA 字段描述结构体。
 */
typedef struct {
    const char *field_name; // 字段名称（便于调试和识别）
    size_t offset;          // 字段在帧中的偏移量（从 CmdSet 和 CmdID 后开始算）
    size_t size;            // 字段长度（字节）
    bool is_required;       // 是否为必须字段
} data_field_t;

// 声明模式切换命令帧和应答帧的字段描述符数组（extern 关键字声明，避免重复定义）
extern const data_field_t MODE_SWITCH_COMMAND_DATA_FIELDS[];
extern const data_field_t MODE_SWITCH_RESPONSE_DATA_FIELDS[];
extern const data_field_t GET_VERSION_COMMAND_DATA_FIELDS[];
extern const data_field_t GET_VERSION_RESPONSE_DATA_FIELDS[];

#endif // DJI_PROTOCOL_DATA_STRUCTURES_H
