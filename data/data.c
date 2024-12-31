#include "data.h"
#include "ble.h"
#include "dji_protocol_parser.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "string.h"

#define TAG "DATA_LAYER"

/* 最大并行等待的命令数量 */
#define MAX_SEQ_ENTRIES 10

/* 条目结构 */
typedef struct {
    bool in_use;
    uint16_t seq;
    cJSON *parse_result;          // 解析后的 cJSON 结果
    SemaphoreHandle_t sem;        // 用于同步等待
} seq_entry_t;

/* 维护 seq 到解析结果的映射 */
static seq_entry_t s_seq_entries[MAX_SEQ_ENTRIES];

/* 互斥锁，保护 s_seq_entries */
static SemaphoreHandle_t s_map_mutex = NULL;

/* 初始化 seq_entries */
static void reset_seq_entries(void) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        s_seq_entries[i].in_use = false;
        s_seq_entries[i].seq = 0;
        if (s_seq_entries[i].parse_result) {
            cJSON_Delete(s_seq_entries[i].parse_result);
            s_seq_entries[i].parse_result = NULL;
        }
        if (s_seq_entries[i].sem) {
            vSemaphoreDelete(s_seq_entries[i].sem);
            s_seq_entries[i].sem = NULL;
        }
    }
}

/* 分配一个空闲的 seq_entry */
static seq_entry_t* allocate_seq_entry(uint16_t seq) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_seq_entries[i].in_use) {
            s_seq_entries[i].in_use = true;
            s_seq_entries[i].seq = seq;
            s_seq_entries[i].parse_result = NULL;
            s_seq_entries[i].sem = xSemaphoreCreateBinary();
            if (s_seq_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
                s_seq_entries[i].in_use = false;
                return NULL;
            }
            return &s_seq_entries[i];
        }
    }
    return NULL; // 没有空闲条目
}

/* 查找指定 seq 的条目 */
static seq_entry_t* find_seq_entry(uint16_t seq) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_seq_entries[i].in_use && s_seq_entries[i].seq == seq) {
            return &s_seq_entries[i];
        }
    }
    return NULL;
}

/* 释放一个条目 */
static void free_seq_entry(seq_entry_t *entry) {
    if (entry) {
        entry->in_use = false;
        entry->seq = 0;
        if (entry->parse_result) {
            cJSON_Delete(entry->parse_result);
            entry->parse_result = NULL;
        }
        if (entry->sem) {
            vSemaphoreDelete(entry->sem);
            entry->sem = NULL;
        }
    }
}

/* 数据层初始化 */
void data_init(void) {
    // 初始化互斥锁
    s_map_mutex = xSemaphoreCreateMutex();
    if (s_map_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // 清空 seq_entries
    reset_seq_entries();
}

/* 发送数据帧（有响应） */
esp_err_t data_write_with_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length) {
    if (!raw_data || raw_data_length == 0) {
        ESP_LOGE(TAG, "Invalid data or length");
        return ESP_ERR_INVALID_ARG;
    }

    // 分配一个 seq_entry
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    seq_entry_t *entry = allocate_seq_entry(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free seq_entry, can't write");
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_map_mutex);

    // 发送写命令（有响应）
    esp_err_t ret = ble_write_with_response(
        s_ble_profile.conn_id,           // 当前连接 ID
        s_ble_profile.write_char_handle, // 写特征句柄
        raw_data,                        // 数据
        raw_data_length                  // 长度
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_write_with_response failed: %s", esp_err_to_name(ret));
        // 释放条目
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free_seq_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    return ESP_OK;
}

/* 发送数据帧（无响应） */
esp_err_t data_write_without_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length) {
    if (!raw_data || raw_data_length == 0) {
        ESP_LOGE(TAG, "Invalid raw_data or raw_data_length");
        return ESP_ERR_INVALID_ARG;
    }

    // 分配一个 seq_entry
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    seq_entry_t *entry = allocate_seq_entry(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free seq_entry, can't write");
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_map_mutex);

    // 发送写命令（无响应）
    esp_err_t ret = ble_write_without_response(
        s_ble_profile.conn_id,
        s_ble_profile.write_char_handle,
        raw_data,
        raw_data_length
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_write_without_response failed: %s", esp_err_to_name(ret));
        // 释放条目
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free_seq_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    // 对于无响应写，不等待结果，直接释放条目
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        free_seq_entry(entry);
        xSemaphoreGive(s_map_mutex);
    }

    return ESP_OK;
}

/* 等待特定 seq 的解析结果 */
esp_err_t data_wait_for_result(uint16_t seq, int timeout_ms, cJSON **out_json) {
    if (!out_json) {
        ESP_LOGE(TAG, "out_json is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    seq_entry_t *entry = find_seq_entry(seq);
    if (!entry) {
        ESP_LOGE(TAG, "Entry not found for seq=0x%04X", seq);
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 增加引用计数，防止在等待期间被释放
    xSemaphoreGive(s_map_mutex);

    // 等待信号量被释放
    if (xSemaphoreTake(entry->sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "Wait for seq=0x%04X timed out", seq);
        return ESP_ERR_TIMEOUT;
    }

    // 取出解析结果
    if (entry->parse_result) {
        *out_json = cJSON_Duplicate(entry->parse_result, 1); // 深拷贝
        if (*out_json == NULL) {
            ESP_LOGE(TAG, "Failed to duplicate JSON result");
            return ESP_ERR_NO_MEM;
        }
    }

    // 释放条目
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        free_seq_entry(entry);
        xSemaphoreGive(s_map_mutex);
    }

    return ESP_OK;
}

/* Notify 回调函数，合并到数据层 */
void receive_camera_notify_handler(const uint8_t *raw_data, size_t raw_data_length) {
    if (!raw_data || raw_data_length < 2) {
        ESP_LOGW(TAG, "Notify data is too short or null, skip parse");
        return;
    }

    // 检查帧头
    if (raw_data[0] == 0xAA || raw_data[0] == 0xaa) {
        ESP_LOGI(TAG, "Notification received, attempting to parse...");
        ESP_LOG_BUFFER_HEX(TAG, raw_data, raw_data_length);  // 打印通知内容

        // 定义解析结果结构体
        protocol_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        // 调用 protocol_parse_notification 解析通知帧
        // 假设 protocol_parse_notification 能解析出 frame.seq
        int ret = protocol_parse_notification(raw_data, raw_data_length, &frame); // expected_seq=0 表示不检查
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to parse notification frame, error: %d", ret);
            return;
        }

        // 解析 data 段
        cJSON *parse_result = NULL;
        if (frame.data && frame.data_length > 0) {
            // 假设 protocol_parse_data 返回 cJSON* 类型
            parse_result = protocol_parse_data(frame.data, frame.data_length); 
            if (parse_result == NULL) {
                ESP_LOGE(TAG, "Failed to parse data segment, error: %d", ret);
            } else {
                ESP_LOGI(TAG, "Data segment parsed successfully");
            }
        } else {
            ESP_LOGW(TAG, "Data segment is empty, skipping data parsing");
        }

        // 获取实际的 seq（假设 frame 里有 seq 字段）
        uint16_t actual_seq = frame.seq;
        ESP_LOGI(TAG, "Parsed seq = 0x%04X", actual_seq);

        // 查找对应的条目
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            seq_entry_t *entry = find_seq_entry(actual_seq);
            if (entry) {
                // 假设 parse_result 是 protocol_parse_data 返回的 cJSON 对象
                if (parse_result != NULL) {
                    // 将解析结果放入对应的条目
                    entry->parse_result = parse_result;  // 将 cJSON 结果存储到条目的 value 字段
                    // 唤醒等待的任务
                    xSemaphoreGive(entry->sem);
                } else {
                    ESP_LOGE(TAG, "Parsing data failed, entry not updated");
                }
            } else {
                ESP_LOGW(TAG, "No waiting entry found for seq=0x%04X", actual_seq);
            }
            xSemaphoreGive(s_map_mutex);
        }
    } else {
        // ESP_LOGW(TAG, "Received frame does not start with 0xAA, ignoring...");
    }
}