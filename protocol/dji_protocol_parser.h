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

#ifndef DJI_PROTOCOL_PARSER_H
#define DJI_PROTOCOL_PARSER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Protocol frame parsing result structure
 *        协议帧解析结果结构体
 */
typedef struct {
    uint8_t sof;            // Start of frame (SOF)
                            // 帧头 (SOF)
    uint16_t version;       // Protocol version
                            // 版本号
    uint16_t frame_length;  // Total frame length
                            // 帧长度
    uint8_t cmd_type;       // Command type (CmdType)
                            // 命令类型 (CmdType)
    uint8_t enc;            // Encryption flag (ENC)
                            // 加密标志 (ENC)
    uint8_t res[3];         // Reserved field (RES)
                            // 保留字段 (RES)
    uint16_t seq;           // Sequence number (SEQ)
                            // 序列号 (SEQ)
    uint16_t crc16;         // CRC-16 checksum
                            // CRC-16 校验值
    const uint8_t *data;    // Pointer to data segment (DATA)
                            // 数据段指针 (DATA)
    size_t data_length;     // Length of data segment
                            // 数据段长度
    uint32_t crc32;         // CRC-32 checksum
                            // CRC-32 校验值
} protocol_frame_t;

int protocol_parse_notification(const uint8_t *frame_data, size_t frame_length, protocol_frame_t *frame_out);

void* protocol_parse_data(const uint8_t *data, size_t data_length, uint8_t cmd_type, size_t *data_length_without_cmd_out);

uint8_t* protocol_create_frame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, uint16_t seq, size_t *frame_length_out);

#endif