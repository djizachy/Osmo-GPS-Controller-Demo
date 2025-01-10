#ifndef DJI_PROTOCOL_PARSER_H
#define DJI_PROTOCOL_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"

/**
 * @brief 协议帧解析结果结构体
 */
typedef struct {
    uint8_t sof;            // 帧头 (SOF)
    uint16_t version;       // 版本号
    uint16_t frame_length;  // 帧长度
    uint8_t cmd_type;       // 命令类型 (CmdType)
    uint8_t enc;            // 加密标志 (ENC)
    uint8_t res[3];         // 保留字段 (RES)
    uint16_t seq;           // 序列号 (SEQ)
    uint16_t crc16;         // CRC-16 校验值
    const uint8_t *data;    // 数据段指针 (DATA)
    size_t data_length;     // 数据段长度
    uint32_t crc32;         // CRC-32 校验值
} protocol_frame_t;

/**
 * @brief 解析通知数据。
 * @param frame_data 接收到的完整通知数据。
 * @param frame_length 数据长度。
 * @param frame 输出的解析结果结构体。
 * @return 0 表示成功，非 0 表示错误。
 */
int protocol_parse_notification(const uint8_t *frame_data, size_t frame_length, protocol_frame_t *frame);

/**
 * @brief 解析协议数据段 (DATA 字段) 并返回解析结果的 JSON 对象。
 * 
 * 该函数根据传入的数据段解析 `CmdSet` 和 `CmdID` 对应的字段，使用 `cJSON` 格式返回解析结果。
 * 如果数据解析成功，返回包含解析字段的 `cJSON` 对象；如果解析失败，返回 `NULL`。
 * 
 * @param data 数据段指针，包含要解析的完整数据。
 * @param data_length 数据段的长度，至少为 2（包含 CmdSet 和 CmdID 字段）。
 * @param cmd_type 命令类型
 * @return 返回一个 `cJSON` 对象，包含解析后的数据；如果解析失败，返回 `NULL`。
 */
cJSON* protocol_parse_data(const uint8_t *data, size_t data_length, uint8_t cmd_type);


/**
 * @brief 创建协议帧。
 * 
 * @param cmd_set 命令集标识符。
 * @param cmd_id 命令标识符。
 * @param cmd_type 命令类型标识符（如请求、应答）。
 * @param key_values_or_structure 输入的 key-value 数组，包含待封装的数据（可能是 cJSON 也可能是结构体）。
 * @param seq 帧的序列号。
 * @param frame_length 生成的帧总长度（包括协议头、有效载荷和校验码）。
 * @param create_mode 创建方式，0为无结构体法，1为结构体法。
 * @return 动态分配的帧数据（需要外部释放），如果失败返回 NULL。
 */
uint8_t* protocol_create_frame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *key_values_or_structure, uint16_t seq, size_t *frame_length, uint8_t create_mode);

#endif
