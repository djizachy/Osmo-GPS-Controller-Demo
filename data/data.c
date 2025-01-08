#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "string.h"
#include "cJSON.h"
#include "data.h"
#include "ble.h"
#include "dji_protocol_parser.h"

#define TAG "DATA_LAYER"

/* 最大并行等待的命令数量 */
#define MAX_SEQ_ENTRIES 10
/* 定时删除的周期（单位：毫秒） */
#define CLEANUP_INTERVAL_MS 60000 // 每60秒清理一次
/* 最长保留时间（单位：秒），超过此时间没有被使用的条目会被清除 */
#define MAX_ENTRY_AGE 120

/* 条目结构 */
typedef struct {
    bool in_use;
    bool is_seq_based;            // 新增：true 表示基于 seq，false 表示基于 cmd_set 和 cmd_id
    uint16_t seq;                 // 如果 is_seq_based 为 true，则有效
    uint8_t cmd_set;             // 如果 is_seq_based 为 false，则有效
    uint8_t cmd_id;              // 如果 is_seq_based 为 false，则有效
    cJSON *parse_result;          // 解析后的 cJSON 结果
    SemaphoreHandle_t sem;        // 用于同步等待
    TickType_t last_access_time;  // 最近访问的时间戳，用于 LRU 策略
} entry_t;

/* 维护 seq 到解析结果的映射 */
static entry_t s_entries[MAX_SEQ_ENTRIES];

/* 互斥锁，保护 s_seq_entries */
static SemaphoreHandle_t s_map_mutex = NULL;

/* 定时器句柄 */
static TimerHandle_t cleanup_timer = NULL;

/* 初始化 seq_entries */
static void reset_entries(void) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        s_entries[i].in_use = false;
        s_entries[i].is_seq_based = false;
        s_entries[i].seq = 0;
        s_entries[i].cmd_set = 0;
        s_entries[i].cmd_id = 0;
        s_entries[i].last_access_time = 0;
        if (s_entries[i].parse_result) {
            cJSON_Delete(s_entries[i].parse_result);
            s_entries[i].parse_result = NULL;
        }
        if (s_entries[i].sem) {
            vSemaphoreDelete(s_entries[i].sem);
            s_entries[i].sem = NULL;
        }
    }
}

/* 查找指定 seq 的条目 */
static entry_t* find_entry_by_seq(uint16_t seq) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && s_entries[i].is_seq_based && s_entries[i].seq == seq) {
            s_entries[i].last_access_time = xTaskGetTickCount();  // 更新最近访问时间
            return &s_entries[i];
        }
    }
    return NULL;
}

/* 查找指定 cmd_set 和 cmd_id 的条目 */
static entry_t* find_entry_by_cmd_id(uint16_t cmd_set, uint16_t cmd_id) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && !s_entries[i].is_seq_based && 
            s_entries[i].cmd_set == cmd_set && s_entries[i].cmd_id == cmd_id) {
            s_entries[i].last_access_time = xTaskGetTickCount();  // 更新最近访问时间
            return &s_entries[i];
        }
    }
    return NULL;
}

/* 释放一个条目 */
static void free_entry(entry_t *entry) {
    if (entry) {
        entry->in_use = false;
        entry->is_seq_based = false;
        entry->seq = 0;
        entry->cmd_set = 0;
        entry->cmd_id = 0;
        entry->last_access_time = 0;
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

/* 分配一个空闲的 entry，基于 seq 或 cmd_set/cmd_id */
static entry_t* allocate_entry_by_seq(uint16_t seq) {
    // 首先检查是否已存在相同 seq 的条目
    entry_t *existing_entry = find_entry_by_seq(seq);
    if (existing_entry) {
        // 覆盖现有条目
        ESP_LOGI(TAG, "Overwriting existing entry for seq=0x%04X", seq);
        free_entry(existing_entry);
        // 继续分配新的条目
    }

    entry_t* oldest_entry = NULL;  // 用于记录最久未使用的条目
    TickType_t oldest_access_time = xTaskGetTickCount(); // 初始时间为当前时间

    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_entries[i].in_use) {
            // 找到一个空闲条目
            s_entries[i].in_use = true;
            s_entries[i].is_seq_based = true;
            s_entries[i].seq = seq;
            s_entries[i].cmd_set = 0;
            s_entries[i].cmd_id = 0;
            s_entries[i].parse_result = NULL;
            s_entries[i].sem = xSemaphoreCreateBinary();
            if (s_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
                s_entries[i].in_use = false;
                return NULL;
            }
            s_entries[i].last_access_time = xTaskGetTickCount();  // 设置为当前时间
            return &s_entries[i];
        }

        // 寻找最久未使用的条目
        if (s_entries[i].last_access_time < oldest_access_time) {
            oldest_access_time = s_entries[i].last_access_time;
            oldest_entry = &s_entries[i];
        }
    }

    // 如果没有空闲条目，则删除最久未使用的条目
    if (oldest_entry) {
        ESP_LOGW(TAG, "Deleting the least recently used entry: seq=0x%04X or cmd_set=0x%04X cmd_id=0x%04X",
                 oldest_entry->is_seq_based ? oldest_entry->seq : 0,
                 oldest_entry->cmd_set,
                 oldest_entry->cmd_id);
        free_entry(oldest_entry);
        // 重新分配
        oldest_entry->in_use = true;
        oldest_entry->is_seq_based = true;
        oldest_entry->seq = seq;
        oldest_entry->cmd_set = 0;
        oldest_entry->cmd_id = 0;
        oldest_entry->parse_result = NULL;
        oldest_entry->sem = xSemaphoreCreateBinary();
        if (oldest_entry->sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
            oldest_entry->in_use = false;
            return NULL;
        }
        oldest_entry->last_access_time = xTaskGetTickCount();  // 设置为当前时间
        return oldest_entry;
    }

    return NULL; // 没有空闲条目
}

static entry_t* allocate_entry_by_cmd(uint8_t cmd_set, uint8_t cmd_id) {
    // 首先检查是否已存在相同 cmd_set 和 cmd_id 的条目
    entry_t *existing_entry = find_entry_by_cmd_id(cmd_set, cmd_id);
    if (existing_entry) {
        // 条目已存在，不覆盖
        ESP_LOGI(TAG, "Entry for cmd_set=0x%04X cmd_id=0x%04X already exists", cmd_set, cmd_id);
        return existing_entry;
    }

    // 分配新的条目
    entry_t* oldest_entry = NULL;  // 用于记录最久未使用的非 seq-based 条目
    TickType_t oldest_access_time = xTaskGetTickCount(); // 初始时间为当前时间

    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_entries[i].in_use) {
            // 找到一个空闲条目
            s_entries[i].in_use = true;
            s_entries[i].is_seq_based = false;
            s_entries[i].seq = 0;
            s_entries[i].cmd_set = cmd_set;
            s_entries[i].cmd_id = cmd_id;
            s_entries[i].parse_result = NULL;
            s_entries[i].sem = xSemaphoreCreateBinary();
            if (s_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
                s_entries[i].in_use = false;
                return NULL;
            }
            s_entries[i].last_access_time = xTaskGetTickCount();  // 设置为当前时间
            return &s_entries[i];
        }

        // 仅考虑非基于 seq 的条目作为候选删除对象
        if (!s_entries[i].is_seq_based && s_entries[i].last_access_time < oldest_access_time) {
            oldest_access_time = s_entries[i].last_access_time;
            oldest_entry = &s_entries[i];
        }
    }

    // 如果没有空闲条目，则尝试删除最久未使用的非 seq-based 条目
    if (oldest_entry) {
        ESP_LOGW(TAG, "Deleting the least recently used cmd-based entry: cmd_set=0x%04X cmd_id=0x%04X",
                 oldest_entry->cmd_set,
                 oldest_entry->cmd_id);
        free_entry(oldest_entry);

        // 重新分配被删除的条目
        oldest_entry->in_use = true;
        oldest_entry->is_seq_based = false;
        oldest_entry->seq = 0;
        oldest_entry->cmd_set = cmd_set;
        oldest_entry->cmd_id = cmd_id;
        oldest_entry->parse_result = NULL;
        oldest_entry->sem = xSemaphoreCreateBinary();
        if (oldest_entry->sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
            oldest_entry->in_use = false;
            return NULL;
        }
        oldest_entry->last_access_time = xTaskGetTickCount();  // 设置为当前时间
        return oldest_entry;
    }

    // 如果没有可删除的非 seq-based 条目，则无法分配新的 cmd-based 条目
    ESP_LOGE(TAG, "No available cmd-based entry to allocate for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
    return NULL;
}

/* 定时清理函数 */
static void cleanup_old_entries(TimerHandle_t xTimer) {
    TickType_t current_time = xTaskGetTickCount();
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in cleanup");
        return;
    }
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && (current_time - s_entries[i].last_access_time) > pdMS_TO_TICKS(MAX_ENTRY_AGE * 1000)) {
            if (s_entries[i].is_seq_based) {
                ESP_LOGI(TAG, "Cleaning up unused entry seq=0x%04X", s_entries[i].seq);
            } else {
                ESP_LOGI(TAG, "Cleaning up unused entry cmd_set=0x%04X cmd_id=0x%04X", s_entries[i].cmd_set, s_entries[i].cmd_id);
            }
            free_entry(&s_entries[i]);
        }
    }
    xSemaphoreGive(s_map_mutex);
}

/* 数据层初始化 */
void data_init(void) {
    // 初始化互斥锁
    s_map_mutex = xSemaphoreCreateMutex();
    if (s_map_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // 清空 entries
    reset_entries();

    // 初始化定时器，用于清理过期的条目
    cleanup_timer = xTimerCreate("cleanup_timer", pdMS_TO_TICKS(CLEANUP_INTERVAL_MS), pdTRUE, NULL, cleanup_old_entries);
    if (cleanup_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create cleanup timer");
    } else {
        xTimerStart(cleanup_timer, 0);
    }
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

    entry_t *entry = allocate_entry_by_seq(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free entry, can't write");
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
            free_entry(entry);
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

    entry_t *entry = allocate_entry_by_seq(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free entry, can't write");
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
            free_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    // 对于无响应写，不等待结果，直接释放条目
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        free_entry(entry);
        xSemaphoreGive(s_map_mutex);
    }

    return ESP_OK;
}

/* 等待特定 seq 的解析结果 */
esp_err_t data_wait_for_result_by_seq(uint16_t seq, int timeout_ms, cJSON **out_json) {
    if (!out_json) {
        ESP_LOGE(TAG, "out_json is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // 获取互斥锁
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take mutex");
            return ESP_ERR_INVALID_STATE;
        }

        // 尝试查找 entry
        entry_t *entry = find_entry_by_seq(seq);

        if (entry) {
            // 增加引用计数，防止在等待期间被释放
            xSemaphoreGive(s_map_mutex);

            // 等待信号量被释放
            if (xSemaphoreTake(entry->sem, timeout_ticks) != pdTRUE) {
                ESP_LOGW(TAG, "Wait for seq=0x%04X timed out", seq);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
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
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
            }

            return ESP_OK; // 成功返回
        }

        // 如果没有找到 entry，检查是否超时
        TickType_t elapsed_time = xTaskGetTickCount() - start_time;
        if (elapsed_time >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout while waiting for seq=0x%04X, no entry found", seq);
            xSemaphoreGive(s_map_mutex);
            return ESP_ERR_TIMEOUT; // 超时返回
        }

        // 没有找到 entry，释放锁并等待一段时间再重试
        xSemaphoreGive(s_map_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // 等待 10 毫秒后重试
    }
}

/* 等待特定 cmd_set 和 cmd_id 的解析结果，并返回 seq */
esp_err_t data_wait_for_result_by_cmd(uint8_t cmd_set, uint8_t cmd_id, int timeout_ms, cJSON **out_json, uint16_t *out_seq) {
    if (!out_json) {
        ESP_LOGE(TAG, "out_json is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (!out_seq) {
        ESP_LOGE(TAG, "out_seq is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // 获取互斥锁
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take mutex");
            return ESP_ERR_INVALID_STATE;
        }

        // 尝试查找 entry
        entry_t *entry = find_entry_by_cmd_id(cmd_set, cmd_id);

        if (entry) {
            // 增加引用计数，防止在等待期间被释放
            xSemaphoreGive(s_map_mutex);

            // 等待信号量被释放
            if (xSemaphoreTake(entry->sem, timeout_ticks) != pdTRUE) {
                ESP_LOGW(TAG, "Wait for cmd_set=0x%04X cmd_id=0x%04X timed out", cmd_set, cmd_id);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
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

            // 取出 seq
            *out_seq = entry->seq;

            // 释放条目
            if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
            }

            return ESP_OK; // 成功返回
        }

        // 如果没有找到 entry，检查是否超时
        TickType_t elapsed_time = xTaskGetTickCount() - start_time;
        if (elapsed_time >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout while waiting for cmd_set=0x%04X cmd_id=0x%04X, no entry found", cmd_set, cmd_id);
            xSemaphoreGive(s_map_mutex);
            return ESP_ERR_TIMEOUT; // 超时返回
        }

        // 没有找到 entry，释放锁并等待一段时间再重试
        xSemaphoreGive(s_map_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // 等待 10 毫秒后重试
    }
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
        int ret = protocol_parse_notification(raw_data, raw_data_length, &frame);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to parse notification frame, error: %d", ret);
            return;
        }

        // 解析 data 段
        cJSON *parse_result = NULL;
        if (frame.data && frame.data_length > 0) {
            // 假设 protocol_parse_data 返回 cJSON* 类型
            parse_result = protocol_parse_data(frame.data, frame.data_length, frame.cmd_type); 
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
        uint8_t actual_cmd_set = frame.data[0];
        uint8_t actual_cmd_id = frame.data[1];
        ESP_LOGI(TAG, "Parsed seq = 0x%04X, cmd_set=0x%04X, cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);

        // 查找对应的条目
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            entry_t *entry = find_entry_by_seq(actual_seq);
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
                // 相机主动推送来的
                ESP_LOGW(TAG, "No waiting entry found for seq=0x%04X, creating a new entry by cmd_set=0x%04X cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);
                // 分配一个新的条目
                entry = allocate_entry_by_cmd(actual_cmd_set, actual_cmd_id);
                if (entry == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate entry for seq=0x%04X cmd_set=0x%04X cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);
                } else {
                    // 初始化解析结果
                    entry->parse_result = parse_result;
                    entry->seq = actual_seq;
                    entry->last_access_time = xTaskGetTickCount();
                    ESP_LOGI(TAG, "New entry allocated for seq=0x%04X", frame.seq);
                    xSemaphoreGive(entry->sem);
                }
            }
            xSemaphoreGive(s_map_mutex);
        }
    } else {
        // ESP_LOGW(TAG, "Received frame does not start with 0xAA, ignoring...");
    }
}
