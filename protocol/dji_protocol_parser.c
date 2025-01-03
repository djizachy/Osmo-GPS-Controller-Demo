#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

#include "custom_crc16.h"
#include "custom_crc32.h"

#include "dji_protocol_data_processor.h"
#include "dji_protocol_parser.h"

#define TAG "DJI_PROTOCOL_PARSER"

// 协议帧部分长度定义
#define PROTOCOL_SOF_LENGTH          1  // SOF 起始字节
#define PROTOCOL_VER_LEN_LENGTH      2  // Ver/Length 字段
#define PROTOCOL_CMD_TYPE_LENGTH     1  // CmdType
#define PROTOCOL_ENC_LENGTH          1  // ENC 加密字段
#define PROTOCOL_RES_LENGTH          3  // RES 保留字节
#define PROTOCOL_SEQ_LENGTH          2  // SEQ 序列号
#define PROTOCOL_CRC16_LENGTH        2  // CRC-16 校验
#define PROTOCOL_CMD_SET_LENGTH      1  // CmdSet 字段
#define PROTOCOL_CMD_ID_LENGTH       1  // CmdID 字段
#define PROTOCOL_CRC32_LENGTH        4  // CRC-32 校验

// 定义帧头长度（不包含 CmdSet、CmdID 和有效载荷）
#define PROTOCOL_HEADER_LENGTH ( \
    PROTOCOL_SOF_LENGTH + \
    PROTOCOL_VER_LEN_LENGTH + \
    PROTOCOL_CMD_TYPE_LENGTH + \
    PROTOCOL_ENC_LENGTH + \
    PROTOCOL_RES_LENGTH + \
    PROTOCOL_SEQ_LENGTH + \
    PROTOCOL_CRC16_LENGTH + \
    PROTOCOL_CMD_SET_LENGTH + \
    PROTOCOL_CMD_ID_LENGTH \
)

// 定义帧尾长度（仅包含 CRC-32）
#define PROTOCOL_TAIL_LENGTH PROTOCOL_CRC32_LENGTH

// 定义帧总长度宏（动态计算，包含 DATA 段）
#define PROTOCOL_FULL_FRAME_LENGTH(data_length) ( \
    PROTOCOL_HEADER_LENGTH + \
    (data_length) + \
    PROTOCOL_TAIL_LENGTH \
)

int protocol_parse_notification(const uint8_t *frame_data, size_t frame_length, protocol_frame_t *frame) {
    // 检查最小帧长度
    if (frame_length < 16) { // SOF(1) + Ver/Length(2) + CmdType(1) + ENC(1) + RES(3) + SEQ(2) + CRC-16(2) + CRC-32(4)
        ESP_LOGE(TAG, "Frame too short to be valid");
        return -1;
    }

    // 检查帧头 (SOF)
    if (frame_data[0] != 0xAA) {
        ESP_LOGE(TAG, "Invalid SOF: 0x%02X", frame_data[0]);
        return -2;
    }

    // 解析 Ver/Length
    uint16_t ver_length = (frame_data[2] << 8) | frame_data[1];
    uint16_t version = ver_length >> 10; // 高 6 位为版本号
    uint16_t expected_length = ver_length & 0x03FF; // 低 10 位为帧长度

    if (expected_length != frame_length) {
        ESP_LOGE(TAG, "Frame length mismatch: expected %u, got %zu", expected_length, frame_length);
        return -3;
    }

    // 验证 CRC-16
    uint16_t crc16_received = (frame_data[11] << 8) | frame_data[10];
    uint16_t crc16_calculated = calculate_crc16(frame_data, 10); // 从 SOF 到 SEQ
    if (crc16_received != crc16_calculated) {
        ESP_LOGE(TAG, "CRC-16 mismatch: received 0x%04X, calculated 0x%04X", crc16_received, crc16_calculated);
        return -4;
    }

    // 验证 CRC-32
    uint32_t crc32_received = (frame_data[frame_length - 1] << 24) | (frame_data[frame_length - 2] << 16) |
                              (frame_data[frame_length - 3] << 8) | frame_data[frame_length - 4];
    uint32_t crc32_calculated = calculate_crc32(frame_data, frame_length - 4); // 从 SOF 到 DATA
    if (crc32_received != crc32_calculated) {
        ESP_LOGE(TAG, "CRC-32 mismatch: received 0x%08X, calculated 0x%08X", (unsigned int)crc32_received, (unsigned int)crc32_calculated);
        return -5;
    }

    // 检查序列号
    uint16_t seq_received = (frame_data[8] << 8) | frame_data[9];
    // if (seq_received != exp_seq) {
    //     ESP_LOGE(TAG, "Sequence number mismatch: expected 0x%04X, got 0x%04X", seq, seq_received);
    //     return -6;
    // }

    // 填充解析结果到结构体
    frame->sof = frame_data[0];
    frame->version = version;
    frame->frame_length = expected_length;
    frame->cmd_type = frame_data[3];
    frame->enc = frame_data[4];
    memcpy(frame->res, &frame_data[5], 3);
    frame->seq = seq_received;
    frame->crc16 = crc16_received;

    // 处理数据段 (DATA)
    if (frame_length > 16) { // DATA 段存在
        frame->data = &frame_data[12];
        frame->data_length = frame_length - 16; // DATA 长度
    } else { // DATA 段为空
        frame->data = NULL;
        frame->data_length = 0;
        ESP_LOGW(TAG, "DATA segment is empty");
    }

    frame->crc32 = crc32_received;

    ESP_LOGI(TAG, "Frame parsed successfully");
    return 0;
}

cJSON* protocol_parse_data(const uint8_t *data, size_t data_length) {
    if (data == NULL || data_length < 2) {
        ESP_LOGE(TAG, "Invalid data segment: data is NULL or too short");
        return NULL;  // 返回 NULL，表示解析失败
    }

    // 提取 CmdSet 和 CmdID
    uint8_t cmd_set = data[0];
    uint8_t cmd_id = data[1];

    // 查找对应的命令描述符
    const data_descriptor_t *descriptor = find_descriptor(cmd_set, cmd_id);

    // 如果找不到描述符，尝试通过结构体描述符查找
    const structure_descriptor_t *structure_descriptor = NULL;
    
    if (descriptor == NULL) {
        ESP_LOGW(TAG, "No descriptor found for CmdSet 0x%02X and CmdID 0x%02X, trying structure descriptor", cmd_set, cmd_id);
        structure_descriptor = find_descriptor_by_structure(cmd_set, cmd_id);
        if (structure_descriptor == NULL) {
            ESP_LOGE(TAG, "No structure descriptor found for CmdSet 0x%02X and CmdID 0x%02X", cmd_set, cmd_id);
            return NULL;  // 两种描述符都找不到，返回 NULL
        }
    }

    // 取出应答帧数据
    const uint8_t *response_data = &data[2];
    size_t response_length = data_length - 2;

    ESP_LOGI(TAG, "CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);

    // 使用 cJSON 创建一个 JSON 对象来存储响应数据
    cJSON *response_json = cJSON_CreateObject();

    // 调用解析函数
    int result = -1;  // 用于存储解析结果
    if (descriptor != NULL) {
        // 如果找到 data_descriptor，则使用通用数据解析函数
        result = data_parser(cmd_set, cmd_id, response_data, response_length, response_json);
    } else if (structure_descriptor != NULL && structure_descriptor->parser != NULL) {
        // 如果找到 structure_descriptor，则使用结构体解析函数
        result = structure_descriptor->parser(response_data, response_length, response_json);
    }

    // 检查解析结果
    if (result == 0) {
        // 解析成功
        ESP_LOGI(TAG, "Data parsed successfully for CmdSet 0x%02X and CmdID 0x%02X", cmd_set, cmd_id);
    } else {
        // 解析失败
        ESP_LOGE(TAG, "Failed to parse data for CmdSet 0x%02X and CmdID 0x%02X", cmd_set, cmd_id);
        cJSON_Delete(response_json);  // 删除 JSON 对象
        return NULL;  // 返回 NULL
    }

    // 返回解析后的 JSON 对象
    return response_json;
}

uint8_t* protocol_create_frame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *key_values_or_structure, uint16_t seq, size_t *frame_length, uint8_t create_mode) {
    size_t data_length = 0;
    uint8_t *payload_data = NULL;

    // 根据创建方式调用不同的函数
    if (create_mode == 0) {
        // 使用 data_creator
        payload_data = data_creator(cmd_set, cmd_id, (const cJSON *)key_values_or_structure, &data_length);
    } else if (create_mode == 1) {
        // 使用 data_creator_by_structure
        payload_data = data_creator_by_structure(cmd_set, cmd_id, key_values_or_structure, &data_length);
    } else {
        // 无效的创建方式
        ESP_LOGE(TAG, "Invalid create_mode: %d", create_mode);
        return NULL;
    }
    
    // payload_data 为 NULL 且 data_length 为 0 时视为空数据段，不报错
    if (payload_data == NULL && data_length > 0) {
        ESP_LOGE(TAG, "Failed to create payload data with non-zero length");
        return NULL;
    }

    // 计算总帧长度
    *frame_length = PROTOCOL_HEADER_LENGTH + data_length + PROTOCOL_TAIL_LENGTH;
    printf("Frame Length: %zu\n", *frame_length);

    // 分配内存以存储整个帧
    uint8_t *frame = (uint8_t *)malloc(*frame_length);
    if (frame == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for protocol frame");
        free(payload_data);
        return NULL;
    }

    // 初始化帧内容
    memset(frame, 0, *frame_length);

    // 填充协议头部
    size_t offset = 0;
    frame[offset++] = 0xAA;  // SOF 起始字节

    // Ver/Length 字段
    uint16_t version = 0;  // 固定版本号
    uint16_t ver_length = (version << 10) | (*frame_length & 0x03FF);
    frame[offset++] = ver_length & 0xFF;        // Ver/Length 低字节
    frame[offset++] = (ver_length >> 8) & 0xFF; // Ver/Length 高字节

    // CmdType
    frame[offset++] = cmd_type;

    // ENC（不加密，固定 0）
    frame[offset++] = 0x00;

    // RES（保留字节，固定 0）
    frame[offset++] = 0x00;
    frame[offset++] = 0x00;
    frame[offset++] = 0x00;

    // SEQ 序列号
    frame[offset++] = (seq >> 8) & 0xFF; // 序列号高字节
    frame[offset++] = seq & 0xFF;        // 序列号低字节

    // CRC-16 校验（覆盖从 SOF 到 SEQ）
    uint16_t crc16 = calculate_crc16(frame, offset);
    frame[offset++] = crc16 & 0xFF;        // CRC-16 低字节
    frame[offset++] = (crc16 >> 8) & 0xFF; // CRC-16 高字节

    // 填充 CmdSet 和 CmdID
    frame[offset++] = cmd_set;
    frame[offset++] = cmd_id;

    // 填充有效载荷数据
    memcpy(&frame[offset], payload_data, data_length);
    offset += data_length;

    // CRC-32 校验（覆盖从 SOF 到 DATA 的所有部分）
    uint32_t crc32 = calculate_crc32(frame, offset);
    frame[offset++] = crc32 & 0xFF;          // CRC-32 第 1 字节
    frame[offset++] = (crc32 >> 8) & 0xFF;   // CRC-32 第 2 字节
    frame[offset++] = (crc32 >> 16) & 0xFF;  // CRC-32 第 3 字节
    frame[offset++] = (crc32 >> 24) & 0xFF;  // CRC-32 第 4 字节

    // 释放有效载荷数据
    free(payload_data);

    return frame;
}
