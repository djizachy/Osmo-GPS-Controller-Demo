#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "data.h"
#include "ble.h"
#include "dji_protocol_parser.h"

#define TAG "DATA"

/* 最大并行等待的命令数量 */
/* Maximum number of commands that can be waited in parallel */
#define MAX_SEQ_ENTRIES 10

/* 定时删除的周期（单位：毫秒） */
/* Cleanup interval in milliseconds */
#define CLEANUP_INTERVAL_MS 60000

/* 最长保留时间（单位：秒），超过此时间没有被使用的条目会被清除 */
/* Maximum retention time in seconds, entries unused beyond this time will be cleared */
#define MAX_ENTRY_AGE 120

static bool data_layer_initialized = false;

/* 条目结构 */
/* Entry structure */
typedef struct {
    // 是否有效
    // Whether the entry is valid
    bool in_use;

    // 新增：true 表示基于 seq，false 表示基于 cmd_set 和 cmd_id
    // Added: true means based on seq, false means based on cmd_set and cmd_id
    bool is_seq_based;

    // 如果 is_seq_based 为 true，则有效
    // Valid if is_seq_based is true
    uint16_t seq;

    // 如果 is_seq_based 为 false，则有效
    // Valid if is_seq_based is false
    uint8_t cmd_set;

    // 如果 is_seq_based 为 false，则有效
    // Valid if is_seq_based is false
    uint8_t cmd_id;

    // 解析后的通用结构体
    // Generic structure after parsing
    void *parse_result;

    // 解析结果的长度
    // Length of parsed result
    size_t parse_result_length;

    // 用于同步等待
    // For synchronous waiting
    SemaphoreHandle_t sem;

    // 最近访问的时间戳，用于 LRU 策略
    // Last access timestamp for LRU policy
    TickType_t last_access_time;
} entry_t;

/* 维护 seq 到解析结果的映射 */
/* Maintains mapping from seq to parsed results */
static entry_t s_entries[MAX_SEQ_ENTRIES];

/* 互斥锁，保护 s_seq_entries */
/* Mutex to protect s_seq_entries */
static SemaphoreHandle_t s_map_mutex = NULL;

/* 定时器句柄 */
/* Timer handle */
static TimerHandle_t cleanup_timer = NULL;

/**
 * @brief Initialize seq_entries and mark all entries as unused
 *        初始化 seq_entries，将所有条目标记为未使用
 */
static void reset_entries(void) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        s_entries[i].in_use = false;
        s_entries[i].is_seq_based = false;
        s_entries[i].seq = 0;
        s_entries[i].cmd_set = 0;
        s_entries[i].cmd_id = 0;
        s_entries[i].last_access_time = 0;
        if (s_entries[i].parse_result) {
            free(s_entries[i].parse_result);
            s_entries[i].parse_result = NULL;
        }
        s_entries[i].parse_result_length = 0;
        if (s_entries[i].sem) {
            vSemaphoreDelete(s_entries[i].sem);
            s_entries[i].sem = NULL;
        }
    }
}

/**
 * @brief Find entry by sequence number
 *        查找指定 seq 的条目
 * 
 * @param seq Sequence number to find
 *            需要查找的 seq 值
 * @return entry_t* Pointer to found entry, NULL if not found
 *                  找到的条目指针，未找到则返回 NULL
 */
static entry_t* find_entry_by_seq(uint16_t seq) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && s_entries[i].is_seq_based && s_entries[i].seq == seq) {
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Find entry by command set and ID
 *        查找指定 cmd_set 和 cmd_id 的条目
 * 
 * @param cmd_set Command set
 *                命令集
 * @param cmd_id Command ID
 *               命令 ID
 * @return entry_t* Pointer to found entry, NULL if not found
 *                  找到的条目指针，未找到则返回 NULL
 */
static entry_t* find_entry_by_cmd_id(uint16_t cmd_set, uint16_t cmd_id) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && !s_entries[i].is_seq_based && 
            s_entries[i].cmd_set == cmd_set && s_entries[i].cmd_id == cmd_id) {
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Free an entry
 *        释放一个条目
 * 
 * @param entry Pointer to the entry to be freed
 *              要释放的条目指针
 */
static void free_entry(entry_t *entry) {
    if (entry) {
        entry->in_use = false;
        entry->is_seq_based = false;
        entry->seq = 0;
        entry->cmd_set = 0;
        entry->cmd_id = 0;
        entry->last_access_time = 0;
        if (entry->parse_result) {
            free(entry->parse_result);
            entry->parse_result = NULL;
        }
        entry->parse_result_length = 0;
        if (entry->sem) {
            vSemaphoreDelete(entry->sem);
            entry->sem = NULL;
        }
    }
}

/**
 * @brief Allocate a free entry based on sequence number
 *        分配一个空闲的 entry，基于 seq
 * 
 * @param seq Frame sequence number
 *            帧序列号
 * @return entry_t* Pointer to allocated entry, NULL if failed
 *                  返回分配的条目指针，如果失败则返回 NULL
 */
static entry_t* allocate_entry_by_seq(uint16_t seq) {
    // First check if an entry with the same seq exists
    // 首先检查是否已存在相同 seq 的条目
    entry_t *existing_entry = find_entry_by_seq(seq);
    if (existing_entry) {
        ESP_LOGI(TAG, "Overwriting existing entry for seq=0x%04X", seq);
        free_entry(existing_entry);
    }

    // For tracking the least recently used entry
    // 用于记录最久未使用的条目
    entry_t* oldest_entry = NULL;

    // Initialize with current time
    // 初始时间为当前时间
    TickType_t oldest_access_time = xTaskGetTickCount();

    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_entries[i].in_use) {
            s_entries[i].in_use = true;
            s_entries[i].is_seq_based = true;
            s_entries[i].seq = seq;
            s_entries[i].cmd_set = 0;
            s_entries[i].cmd_id = 0;
            s_entries[i].parse_result = NULL;
            s_entries[i].parse_result_length = 0;
            s_entries[i].sem = xSemaphoreCreateBinary();
            if (s_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
                s_entries[i].in_use = false;
                return NULL;
            }
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }

        // Track the least recently used entry
        // 最久未使用的条目
        if (s_entries[i].last_access_time < oldest_access_time) {
            oldest_access_time = s_entries[i].last_access_time;
            oldest_entry = &s_entries[i];
        }
    }

    // If no free entry, delete the least recently used entry
    // 如果没有空闲条目，则删除最久未使用的条目
    if (oldest_entry) {
        ESP_LOGW(TAG, "Deleting the least recently used entry: seq=0x%04X or cmd_set=0x%04X cmd_id=0x%04X",
                 oldest_entry->is_seq_based ? oldest_entry->seq : 0,
                 oldest_entry->cmd_set,
                 oldest_entry->cmd_id);
        free_entry(oldest_entry);
        // Reallocate
        // 重新分配
        oldest_entry->in_use = true;
        oldest_entry->is_seq_based = true;
        oldest_entry->seq = seq;
        oldest_entry->cmd_set = 0;
        oldest_entry->cmd_id = 0;
        oldest_entry->parse_result = NULL;
        oldest_entry->parse_result_length = 0;
        oldest_entry->sem = xSemaphoreCreateBinary();
        if (oldest_entry->sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
            oldest_entry->in_use = false;
            return NULL;
        }
        oldest_entry->last_access_time = xTaskGetTickCount();
        return oldest_entry;
    }

    return NULL;
}

/**
 * @brief Allocate a free entry based on command set and ID
 *        分配一个空闲的 entry，基于 cmd_set 和 cmd_id
 * 
 * @param cmd_set Command set
 *                命令集
 * @param cmd_id Command ID
 *               命令 ID
 * @return entry_t* Pointer to allocated entry, NULL if failed
 *                  返回分配的条目指针，如果失败则返回 NULL
 */
static entry_t* allocate_entry_by_cmd(uint8_t cmd_set, uint8_t cmd_id) {
    // First check if an entry with the same cmd_set and cmd_id exists
    // 首先检查是否已存在相同 cmd_set 和 cmd_id 的条目
    entry_t *existing_entry = find_entry_by_cmd_id(cmd_set, cmd_id);
    if (existing_entry) {
        // Entry exists, reuse it
        // 条目已存在，复用
        ESP_LOGI(TAG, "Entry for cmd_set=0x%04X cmd_id=0x%04X already exists, it will be overwritten", cmd_set, cmd_id);
        return existing_entry;
    }

    // Allocate new entry
    // 分配新的条目
    entry_t* oldest_entry = NULL;  // For tracking the least recently used non-seq-based entry
                                  // 用于记录最久未使用的非 seq-based 条目
    TickType_t oldest_access_time = xTaskGetTickCount();

    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_entries[i].in_use) {
            // Found a free entry
            // 找到一个空闲条目
            s_entries[i].in_use = true;
            s_entries[i].is_seq_based = false;
            s_entries[i].seq = 0;
            s_entries[i].cmd_set = cmd_set;
            s_entries[i].cmd_id = cmd_id;
            s_entries[i].parse_result = NULL;
            s_entries[i].parse_result_length = 0;
            s_entries[i].sem = xSemaphoreCreateBinary();
            if (s_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
                s_entries[i].in_use = false;
                return NULL;
            }
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }

        // Only consider non-seq-based entries as deletion candidates
        // 仅考虑非基于 seq 的条目作为候选删除对象
        if (!s_entries[i].is_seq_based && s_entries[i].last_access_time < oldest_access_time) {
            oldest_access_time = s_entries[i].last_access_time;
            oldest_entry = &s_entries[i];
        }
    }

    // If no free entry, try to delete the least recently used non-seq-based entry
    // 如果没有空闲条目，则尝试删除最久未使用的非 seq-based 条目
    if (oldest_entry) {
        ESP_LOGW(TAG, "Deleting the least recently used cmd-based entry: cmd_set=0x%04X cmd_id=0x%04X",
                 oldest_entry->cmd_set,
                 oldest_entry->cmd_id);
        free_entry(oldest_entry);

        // Reallocate the deleted entry
        // 重新分配被删除的条目
        oldest_entry->in_use = true;
        oldest_entry->is_seq_based = false;
        oldest_entry->seq = 0;
        oldest_entry->cmd_set = cmd_set;
        oldest_entry->cmd_id = cmd_id;
        oldest_entry->parse_result = NULL;
        oldest_entry->parse_result_length = 0;
        oldest_entry->sem = xSemaphoreCreateBinary();
        if (oldest_entry->sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
            oldest_entry->in_use = false;
            return NULL;
        }
        oldest_entry->last_access_time = xTaskGetTickCount();
        return oldest_entry;
    }

    ESP_LOGE(TAG, "No available cmd-based entry to allocate for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
    return NULL;
}

/**
 * @brief Timer cleanup function
 *        定时清理函数
 * 
 * Clean up expired entries and delete unused entries.
 * Periodically run cleanup tasks to free up memory that is no longer needed.
 * 清理过期的条目，删除未使用的条目。
 * 定期运行清理任务，释放不再需要的内存。
 * 
 * @param xTimer Timer handle that triggered this callback
 *              触发此回调的定时器句柄
 */
static void cleanup_old_entries(TimerHandle_t xTimer) {
    // Get current system tick count
    // 获取当前系统时间计数
    TickType_t current_time = xTaskGetTickCount();
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in cleanup");
        return;
    }
    // Check each entry for expiration
    // 检查每个条目是否过期
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

/**
 * @brief Data layer initialization
 *        数据层初始化
 * 
 * Initialize data layer, including creating mutex, clearing entries, starting cleanup timer task, etc.
 * 初始化数据层，包括创建互斥锁、清空条目、启动定时清理任务等。
 */
void data_init(void) {
    // Initialize mutex
    // 初始化互斥锁
    s_map_mutex = xSemaphoreCreateMutex();
    if (s_map_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Clear all entries
    // 清空所有条目
    reset_entries();

    // Initialize timer for cleaning up expired entries
    // 初始化定时器，用于清理过期的条目
    cleanup_timer = xTimerCreate("cleanup_timer", pdMS_TO_TICKS(CLEANUP_INTERVAL_MS), pdTRUE, NULL, cleanup_old_entries);
    if (cleanup_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create cleanup timer");
    } else {
        xTimerStart(cleanup_timer, 0);
    }

    // Mark data layer as initialized
    // 标记数据层初始化完成
    data_layer_initialized = true;
    ESP_LOGI(TAG, "Data layer initialized successfully");
}

/**
 * @brief Check if data layer is initialized
 *        检查数据层是否已初始化
 * 
 * @return bool Returns true if data layer is initialized, false otherwise
 *              如果数据层已初始化，返回 true；否则返回 false
 */
bool is_data_layer_initialized(void) {
    return data_layer_initialized;
}

/**
 * @brief Send data frame with response
 *        发送数据帧（有响应）
 * 
 * Send data frame to device via BLE and wait for response.
 * 通过 BLE 向设备发送数据帧，并等待响应。
 * 
 * @param seq Frame sequence number
 *            数据帧的序列号
 * @param raw_data Data to be sent
 *                 需要发送的数据
 * @param raw_data_length Length of data
 *                        数据长度
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                    成功返回 ESP_OK，失败返回错误码
 */
esp_err_t data_write_with_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // 验证输入参数
    if (!raw_data || raw_data_length == 0) {
        ESP_LOGE(TAG, "Invalid data or length");
        return ESP_ERR_INVALID_ARG;
    }

    // Take mutex for thread safety
    // 获取互斥锁以保证线程安全
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate an entry for this sequence
    // 为此序列号分配一个条目
    entry_t *entry = allocate_entry_by_seq(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free entry, can't write");
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_map_mutex);

    // Send write command with response
    // 发送写命令（有响应）
    esp_err_t ret = ble_write_with_response(
        s_ble_profile.conn_id,           // Current connection ID
                                         // 当前连接 ID
        s_ble_profile.write_char_handle, // Write characteristic handle
                                         // 写特征句柄
        raw_data,                        // Data to be sent
                                         // 要发送的数据
        raw_data_length                  // Length of data
                                         // 数据长度
    );

    // Handle write failure
    // 处理写入失败的情况
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_write_with_response failed: %s", esp_err_to_name(ret));
        // Clean up on failure
        // 失败时清理资源
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief Send data frame without response
 *        发送数据帧（无响应）
 * 
 * Send data frame to device via BLE without waiting for response.
 * 通过 BLE 向设备发送数据帧，且不等待响应。
 * 
 * @param seq Frame sequence number
 *            数据帧的序列号
 * @param raw_data Data to be sent
 *                 需要发送的数据
 * @param raw_data_length Length of data
 *                        数据长度
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                    成功返回 ESP_OK，失败返回错误码
 */
esp_err_t data_write_without_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // 验证输入参数
    if (!raw_data || raw_data_length == 0) {
        ESP_LOGE(TAG, "Invalid raw_data or raw_data_length");
        return ESP_ERR_INVALID_ARG;
    }

    // Take mutex for thread safety
    // 获取互斥锁以保证线程安全
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate an entry for this sequence
    // 为此序列号分配一个条目
    entry_t *entry = allocate_entry_by_seq(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free entry, can't write");
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_map_mutex);

    // Send write command without response
    // 发送写命令（无响应）
    esp_err_t ret = ble_write_without_response(
        s_ble_profile.conn_id,           // Current connection ID
                                         // 当前连接 ID
        s_ble_profile.write_char_handle, // Write characteristic handle
                                         // 写特征句柄
        raw_data,                        // Data to be sent
                                         // 要发送的数据
        raw_data_length                  // Length of data
                                         // 数据长度
    );

    // Handle write failure
    // 处理写入失败的情况
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_write_without_response failed: %s", esp_err_to_name(ret));
        // Clean up on failure
        // 失败时清理资源
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    // For write without response, release entry immediately
    // 对于无响应写，立即释放条目
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        free_entry(entry);
        xSemaphoreGive(s_map_mutex);
    }

    return ESP_OK;
}

/**
 * @brief Wait for parsing result of specific sequence number
 *        等待特定 seq 的解析结果
 * 
 * Wait for parsing result of a specific sequence number and return to caller.
 * 等待一个特定 seq 的解析结果，并返回给调用者。
 * 
 * @param seq Frame sequence number
 *            数据帧的序列号
 * @param timeout_ms Timeout in milliseconds
 *                   等待的超时时间（毫秒）
 * @param out_result Return parsed result
 *                   返回解析结果
 * @param out_result_length Return length of parsed result
 *                         返回解析结果的长度
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                    成功返回 ESP_OK，失败返回错误码
 */
esp_err_t data_wait_for_result_by_seq(uint16_t seq, int timeout_ms, void **out_result, size_t *out_result_length) {
    // Validate input parameters
    // 验证输入参数
    if (!out_result || !out_result_length) {
        ESP_LOGE(TAG, "out_result or out_result_length is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Get start time and calculate timeout ticks
    // 获取开始时间并计算超时时钟数
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // Take mutex for thread safety
        // 获取互斥锁以保证线程安全
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take mutex");
            return ESP_ERR_INVALID_STATE;
        }

        // Try to find entry
        // 尝试查找条目
        entry_t *entry = find_entry_by_seq(seq);

        if (entry) {
            // Increase reference count to prevent release during waiting
            // 增加引用计数，防止在等待期间被释放
            xSemaphoreGive(s_map_mutex);

            // Wait for semaphore to be released
            // 等待信号量被释放
            if (xSemaphoreTake(entry->sem, timeout_ticks) != pdTRUE) {
                ESP_LOGW(TAG, "Wait for seq=0x%04X timed out", seq);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_TIMEOUT;
            }

            // Get parsing result
            // 取出解析结果
            if (entry->parse_result) {
                // Allocate new memory for out_result
                // 为 out_result 分配新内存
                *out_result = malloc(entry->parse_result_length);
                if (*out_result == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for out_result");
                    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        free_entry(entry);
                        xSemaphoreGive(s_map_mutex);
                    }
                    return ESP_ERR_NO_MEM;
                }

                // Copy entry->parse_result data to out_result
                // 拷贝 entry->parse_result 数据到 out_result
                memcpy(*out_result, entry->parse_result, entry->parse_result_length);
                *out_result_length = entry->parse_result_length;  // Set length
                                                                 // 设置长度
            } else {
                ESP_LOGE(TAG, "Parse result is NULL for seq=0x%04X", seq);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_NOT_FOUND;
            }

            // Free entry
            // 释放条目
            if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
            }

            return ESP_OK;
        }

        // Check for timeout if entry not found
        // 如果没有找到条目，检查是否超时
        TickType_t elapsed_time = xTaskGetTickCount() - start_time;
        if (elapsed_time >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout while waiting for seq=0x%04X, no entry found", seq);
            xSemaphoreGive(s_map_mutex);
            return ESP_ERR_TIMEOUT;
        }

        // Entry not found, release lock and wait before retry
        // 没有找到条目，释放锁并等待一段时间再重试
        xSemaphoreGive(s_map_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms before retry
                                       // 等待 10 毫秒后重试
    }
}

/**
 * @brief Wait for parsing result by command set and ID, and return sequence number
 *        等待特定 cmd_set 和 cmd_id 的解析结果，并返回 seq
 * 
 * Wait for parsing result of a specific command set and ID, and return its corresponding sequence number.
 * 等待一个特定 cmd_set 和 cmd_id 的解析结果，并返回其对应的 seq 值。
 * 
 * @param cmd_set Command set
 *                命令集
 * @param cmd_id Command ID
 *               命令 ID
 * @param timeout_ms Timeout in milliseconds
 *                   等待的超时时间（毫秒）
 * @param out_seq Return sequence number
 *                返回的 seq 值
 * @param out_result Return parsed result
 *                   返回解析结果
 * @param out_result_length Return length of parsed result
 *                         返回解析结果的长度
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                    成功返回 ESP_OK，失败返回错误码
 */
esp_err_t data_wait_for_result_by_cmd(uint8_t cmd_set, uint8_t cmd_id, int timeout_ms, uint16_t *out_seq, void **out_result, size_t *out_result_length) {
    // Validate input parameters
    // 验证输入参数
    if (!out_result || !out_seq || !out_result_length) {
        ESP_LOGE(TAG, "out_result, out_seq or out_result_length is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Get start time and calculate timeout ticks
    // 获取开始时间并计算超时时钟数
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // Take mutex for thread safety
        // 获取互斥锁以保证线程安全
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take mutex");
            return ESP_ERR_INVALID_STATE;
        }

        // Try to find entry
        // 尝试查找条目
        entry_t *entry = find_entry_by_cmd_id(cmd_set, cmd_id);

        if (entry) {
            // Increase reference count to prevent release during waiting
            // 增加引用计数，防止在等待期间被释放
            xSemaphoreGive(s_map_mutex);

            // Wait for semaphore to be released
            // 等待信号量被释放
            if (xSemaphoreTake(entry->sem, timeout_ticks) != pdTRUE) {
                ESP_LOGW(TAG, "Wait for cmd_set=0x%04X cmd_id=0x%04X timed out", cmd_set, cmd_id);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_TIMEOUT;
            }

            // Get parsing result
            // 取出解析结果
            if (entry->parse_result) {
                // Allocate new memory for out_result
                // 为 out_result 分配新内存
                *out_result = malloc(entry->parse_result_length);
                if (*out_result == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for out_result");
                    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        free_entry(entry);
                        xSemaphoreGive(s_map_mutex);
                    }
                    return ESP_ERR_NO_MEM;
                }
                // Copy entry->parse_result data to out_result
                // 拷贝 entry->parse_result 数据到 out_result
                memcpy(*out_result, entry->parse_result, entry->parse_result_length);
                *out_result_length = entry->parse_result_length;
            } else {
                ESP_LOGE(TAG, "Parse result is NULL for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_NOT_FOUND;
            }

            // Save sequence number
            // 保存序列号
            *out_seq = entry->seq;

            // Free entry
            // 释放条目
            if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
            }

            return ESP_OK;
        }

        // Check for timeout if entry not found
        // 如果没有找到条目，检查是否超时
        TickType_t elapsed_time = xTaskGetTickCount() - start_time;
        if (elapsed_time >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout while waiting for cmd_set=0x%04X cmd_id=0x%04X, no entry found", cmd_set, cmd_id);
            xSemaphoreGive(s_map_mutex);
            return ESP_ERR_TIMEOUT;
        }

        // Entry not found, release lock and wait before retry
        // 没有找到条目，释放锁并等待一段时间再重试
        xSemaphoreGive(s_map_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms before retry
                                       // 等待 10 毫秒后重试
    }
}

/**
 * @brief Register camera status update callback
 *        注册相机状态更新回调函数
 * 
 * This function registers a callback function for camera status updates. After registration,
 * the callback function will be called to synchronize the latest camera status when specific notifications are received.
 * 此函数用于注册一个相机状态更新的回调函数。注册后，当接收到特定的通知时，会调用该回调函数同步相机最新状态。
 * 
 * @param callback Callback function pointer, pointing to user-defined callback function
 *                 回调函数指针，指向用户定义的回调函数
 */
static camera_status_update_cb_t status_update_callback = NULL;
void data_register_status_update_callback(camera_status_update_cb_t callback) {
    status_update_callback = callback;
}

/**
 * @brief Handle camera notifications and parse data (callback function)
 *        处理相机通知并解析数据（回调函数）
 * 
 * This function processes notification data received from the camera. It first checks if the notification frame
 * is valid (starts with 0xAA), then parses the data segment in the notification frame.
 * If parsing is successful, it saves the result to the corresponding entry and wakes up the waiting task
 * through a semaphore. If no corresponding entry is found, it creates a new entry to store the parsing result.
 * 该函数处理从相机收到的通知数据。它首先检查通知帧是否有效（以 0xAA 开头），然后解析通知帧中的数据段。
 * 如果解析成功，会将结果保存到对应的条目中，并通过信号量唤醒等待的任务。如果没有找到对应的条目，则会创建新的条目来存储解析结果。
 * 
 * @param raw_data Raw notification data
 *                 原始通知数据
 * @param raw_data_length Data length
 *                       数据长度
 */
void receive_camera_notify_handler(const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // 验证输入参数
    if (!raw_data || raw_data_length < 2) {
        ESP_LOGW(TAG, "Notify data is too short or null, skip parse");
        return;
    }

    // Check frame header
    // 检查帧头
    if (raw_data[0] == 0xAA || raw_data[0] == 0xaa) {
        ESP_LOGI(TAG, "Notification received, attempting to parse...");
        ESP_LOG_BUFFER_HEX(TAG, raw_data, raw_data_length);  // Print notification content
                                                             // 打印通知内容
        
        // Define parsing result structure
        // 定义解析结果结构体
        protocol_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        // Call protocol_parse_notification to parse notification frame
        // 调用 protocol_parse_notification 解析通知帧
        int ret = protocol_parse_notification(raw_data, raw_data_length, &frame);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to parse notification frame, error: %d", ret);
            return;
        }

        // Parse data segment
        // 解析数据段
        void *parse_result = NULL;
        size_t parse_result_length = 0;
        if (frame.data && frame.data_length > 0) {
            // Assume protocol_parse_data returns void* type
            // 假设 protocol_parse_data 返回 void* 类型
            parse_result = protocol_parse_data(frame.data, frame.data_length, frame.cmd_type, &parse_result_length);
            if (parse_result == NULL) {
                ESP_LOGE(TAG, "Failed to parse data segment, error: %d", ret);
            } else {
                ESP_LOGI(TAG, "Data segment parsed successfully");
            }
        } else {
            ESP_LOGW(TAG, "Data segment is empty, skipping data parsing");
        }

        // Get actual seq (assuming frame has seq field)
        // 获取实际的 seq（假设 frame 里有 seq 字段）
        uint16_t actual_seq = frame.seq;
        uint8_t actual_cmd_set = frame.data[0];
        uint8_t actual_cmd_id = frame.data[1];
        ESP_LOGI(TAG, "Parsed seq = 0x%04X, cmd_set=0x%04X, cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);

        // Find corresponding entry
        // 查找对应的条目
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            entry_t *entry = find_entry_by_seq(actual_seq);
            if (entry) {
                // Assume parse_result is void* object returned by protocol_parse_data
                // 假设 parse_result 是 protocol_parse_data 返回的 void* 对象
                if (parse_result != NULL) {
                    // Put parsing result into corresponding entry
                    // 将解析结果放入对应的条目
                    entry->parse_result = parse_result;  // Store void* result in entry's value field
                                                       // 将 void* 结果存储到条目的 value 字段
                    entry->parse_result_length = parse_result_length; // Record result length
                                                                     // 记录结果长度
                    // Wake up waiting task
                    // 唤醒等待的任务
                    xSemaphoreGive(entry->sem);
                } else {
                    ESP_LOGE(TAG, "Parsing data failed, entry not updated");
                }
            } else {
                // Camera actively pushed notification
                // 相机主动推送来的
                ESP_LOGW(TAG, "No waiting entry found for seq=0x%04X, creating a new entry by cmd_set=0x%04X cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);
                // Allocate a new entry
                // 分配一个新的条目
                entry = allocate_entry_by_cmd(actual_cmd_set, actual_cmd_id);
                if (entry == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate entry for seq=0x%04X cmd_set=0x%04X cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);
                } else {
                    // Initialize parsing result
                    // 初始化解析结果
                    entry->parse_result = parse_result;
                    entry->parse_result_length = parse_result_length;
                    entry->seq = actual_seq;
                    entry->last_access_time = xTaskGetTickCount();
                    ESP_LOGI(TAG, "New entry allocated for seq=0x%04X", frame.seq);
                    xSemaphoreGive(entry->sem);
                }
            }
            xSemaphoreGive(s_map_mutex);
        }

        // Handle camera actively pushed status
        // 相机主动推送状态处理
        if (actual_cmd_set == 0x1D && actual_cmd_id == 0x02 && status_update_callback) {
            // Create new memory copy for status update callback
            // 为状态更新回调创建新的内存副本
            void *status_copy = NULL;
            if (parse_result != NULL && parse_result_length > 0) {
                status_copy = malloc(parse_result_length);
                if (status_copy != NULL) {
                    memcpy(status_copy, parse_result, parse_result_length);
                    status_update_callback(status_copy);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for status update callback");
                }
            }
        }
    } else {
        // ESP_LOGW(TAG, "Received frame does not start with 0xAA, ignoring...");
    }
}
