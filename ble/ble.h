#ifndef __BLE_H__
#define __BLE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"

/* 连接状态结构体 */
typedef struct {
    bool is_connected;
} connection_status_t;

/* 特征句柄查找状态结构体 */
typedef struct {
    bool notify_char_handle_found;
    bool write_char_handle_found;
} handle_discovery_t;

/* 为了简化，做一个全局的 profile 结构体来管理连接与特征信息 */
typedef struct {
    uint16_t conn_id;
    esp_gatt_if_t gattc_if;

    /* 根据需要记录我们要操作的特征 handle */
    uint16_t notify_char_handle;
    uint16_t write_char_handle;
    uint16_t read_char_handle;

    /* service 的起始和结束 handle */
    uint16_t service_start_handle;
    uint16_t service_end_handle;

    /* 远程设备地址 */
    esp_bd_addr_t remote_bda;

    connection_status_t connection_status;     // 连接状态
    handle_discovery_t handle_discovery;       // 特征句柄查找状态
} ble_profile_t;

extern ble_profile_t s_ble_profile;

/**
 * @brief Notify 回调函数类型，用于接收从远端发来的数据
 *
 * @param data   通知的数据指针
 * @param length 通知的数据长度
 */
typedef void (*ble_notify_callback_t)(const uint8_t *data, size_t length);

/**
 * @brief BLE 客户端初始化
 *
 * @param remote_name 期望连接的远程设备名称
 * @return esp_err_t
 *         - ESP_OK on success
 *         - Others on failure
 */
esp_err_t ble_init(const char *remote_name);

/**
 * @brief 连接到指定名称的设备（若已在扫描中，会自动在扫描到该设备时连接）
 *
 * @note  本接口仅作为演示，如果想主动指定地址连接，可自行扩展接口
 * @return esp_err_t
 */
esp_err_t ble_start_scanning_and_connect(void);

/**
 * @brief 断开连接（如果已经连接）
 *
 * @return esp_err_t
 */
esp_err_t ble_disconnect(void);

/**
 * @brief 读取指定特征
 *
 * @param conn_id  连接 ID（由回调事件或内部管理获得）
 * @param handle   特征的 handle
 * @return esp_err_t
 */
esp_err_t ble_read(uint16_t conn_id, uint16_t handle);

/**
 * @brief 写特征（Write Without Response）
 *
 * @param conn_id   连接 ID
 * @param handle    特征 handle
 * @param data      要写入的数据
 * @param length    数据长度
 * @return esp_err_t
 */
esp_err_t ble_write_without_response(uint16_t conn_id, uint16_t handle, const uint8_t *data, size_t length);

/**
 * @brief 写特征（Write With Response）
 *
 * @param conn_id   连接 ID
 * @param handle    特征 handle
 * @param data      要写入的数据
 * @param length    数据长度
 * @return esp_err_t
 */
esp_err_t ble_write_with_response(uint16_t conn_id, uint16_t handle, const uint8_t *data, size_t length);

/**
 * @brief 注册（开启）Notify
 *
 * @param conn_id   连接 ID
 * @param char_handle 需要开启通知的特征 handle
 * @return esp_err_t
 */
esp_err_t ble_register_notify(uint16_t conn_id, uint16_t char_handle);

/**
 * @brief 反注册（关闭）Notify
 *
 * @note  此处仅示例逻辑，需要特征的 Client Config 描述符 handle 来进行操作
 *        若实际项目需要，也可直接先前保存 descr handle，然后在此进行关闭写 0x0000
 *
 * @param conn_id   连接 ID
 * @param char_handle 需要关闭通知的特征 handle
 * @return esp_err_t
 */
esp_err_t ble_unregister_notify(uint16_t conn_id, uint16_t char_handle);

/**
 * @brief 设置全局的 Notify 回调（用于接收数据）
 *
 * @param cb 回调函数指针
 */
void ble_set_notify_callback(ble_notify_callback_t cb);

#endif
