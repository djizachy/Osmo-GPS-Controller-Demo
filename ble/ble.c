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

#include <string.h>
#include "ble.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define TAG "BLE"

/* Target device name */
/* 目标设备名称 */
static char s_remote_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = {0};

/* Flags indicating whether a connection has been initiated and whether the target service has been found, for demonstration only */
/* 是否已发起连接、是否已找到目标服务等标记，仅作演示 */
static bool s_connecting = false;

/* Globally saved Notify callback */
/* 全局保存的 Notify 回调 */
static ble_notify_callback_t s_notify_cb = NULL;

/* Set logic layer disconnection state callback */
/* 设置逻辑层断开连接状态回调 */
static connect_logic_state_callback_t s_state_cb = NULL;

/* Attempt to connect when the target device is scanned */
/* 扫描到目标设备，尝试连接 */
#define MIN_RSSI_THRESHOLD -80          // Set minimum signal strength threshold, adjust as needed
static esp_bd_addr_t best_addr = {0};   // Store the address of the device with the strongest signal
static int8_t best_rssi = -128;         // Store the RSSI value of the device with the strongest signal, initialized to the weakest signal strength
static bool s_is_reconnecting = false;  // Whether in reconnection mode
static bool s_found_previous_device = false;  // Whether the original device was found in reconnection mode

/* Only one profile is stored */
/* 仅存一个 profile */
ble_profile_t s_ble_profile = {
    .conn_id = 0,
    .gattc_if = ESP_GATT_IF_NONE,
    .notify_char_handle = 0,
    .write_char_handle = 0,
    .read_char_handle = 0,
    .service_start_handle = 0,
    .service_end_handle = 0,
    .connection_status = {
        .is_connected = false,
    },
    .handle_discovery = {
        .notify_char_handle_found = false,
        .write_char_handle_found = false,
    },
};

/* Define the Service/Characteristic UUIDs to filter, for search use */
/* 这里定义想要过滤的 Service/Characteristic UUID，供搜索使用 */
#define REMOTE_TARGET_SERVICE_UUID   0xFFF0
#define REMOTE_NOTIFY_CHAR_UUID      0xFFF4
#define REMOTE_WRITE_CHAR_UUID       0xFFF5

static esp_bt_uuid_t s_filter_notify_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = REMOTE_NOTIFY_CHAR_UUID,
};

static esp_bt_uuid_t s_filter_write_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = REMOTE_WRITE_CHAR_UUID,
};

static esp_bt_uuid_t s_notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
};

/* Scan parameters, adjustable as needed */
/* 扫描参数，可根据需求调整 */
static esp_ble_scan_params_t s_ble_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
};

/* Callback function declarations */
/* 回调函数声明 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *param);

static TimerHandle_t scan_timer;

void scan_stop_timer_callback(TimerHandle_t xTimer) {
    esp_ble_gap_stop_scanning();
    ESP_LOGI(TAG, "Scan stopped after timeout");
}

static void trigger_scan_task(void) {
    esp_ble_gap_start_scanning(10);
    // Start a timer to stop scanning after 3 seconds
    // 启动定时器，在3秒后停止扫描
    scan_timer = xTimerCreate("scan_timer", pdMS_TO_TICKS(3000), pdFALSE, (void *)0, scan_stop_timer_callback);
    if (scan_timer != NULL) {
        xTimerStart(scan_timer, 0);
    }
}

/* -------------------------
 *  Initialization/Scan/Connection related interfaces
 *  初始化/扫描/连接相关接口
 * ------------------------- */

/**
 * @brief BLE client initialization
 * BLE 客户端初始化
 *
 * @return esp_err_t
 *         - ESP_OK on success
 *         - Others on failure
 */
esp_err_t ble_init() {
    /* Initialize NVS */
    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Release classic Bluetooth memory */
    /* 释放经典蓝牙内存 */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* Configure and initialize the Bluetooth controller */
    /* 配置并初始化蓝牙控制器 */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start the BLE controller */
    /* 启动 BLE 控制器 */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize the Bluedroid stack */
    /* 初始化 Bluedroid 堆栈 */
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "init bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Enable Bluedroid */
    /* 启用 Bluedroid */
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register GAP callback */
    /* 注册 GAP 回调 */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gap register error, err code = %x", ret);
        return ret;
    }

    /* Register GATTC callback */
    /* 注册 GATTC 回调 */
    ret = esp_ble_gattc_register_callback(gattc_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gattc register error, err code = %x", ret);
        return ret;
    }

    /* Register GATTC application (only one profile here, app_id = 0) */
    /* 注册 GATTC 应用（此处只有一个 profile，app_id = 0） */
    ret = esp_ble_gattc_app_register(0);
    if (ret) {
        ESP_LOGE(TAG, "gattc app register error, err code = %x", ret);
        return ret;
    }

    /* Set local MTU (optional) */
    /* 设置本地 MTU（可选） */
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "ble_init success!");
    return ESP_OK;
}

/**
 * @brief Connect to a device with a specified name (if already scanning, it will automatically connect when the device is found)
 * 连接到指定名称的设备（若已在扫描中，会自动在扫描到该设备时连接）
 *
 * @note  This interface is for demonstration only. If you want to actively specify an address to connect, you can extend the interface yourself.
 *        本接口仅作为演示，如果想主动指定地址连接，可自行扩展接口
 * @return esp_err_t
 */
esp_err_t ble_start_scanning_and_connect(void) {
    /* Reset scan-related variables */
    /* 重置扫描相关变量 */
    memset(best_addr, 0, sizeof(esp_bd_addr_t));
    best_rssi = -128;
    memset(s_remote_device_name, 0, ESP_BLE_ADV_NAME_LEN_MAX);
    s_is_reconnecting = false;
    s_found_previous_device = false;

    /* Set scan parameters */
    /* 设置扫描参数 */
    esp_err_t ret = esp_ble_gap_set_scan_params(&s_ble_scan_params);
    if (ret) {
        ESP_LOGE(TAG, "set scan params error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Starting to scan...");
    return ESP_OK;
}

static void try_to_connect(esp_bd_addr_t addr) {
    // Check if already connecting
    // 检查是否正在连接中
    if (s_connecting) {
        ESP_LOGW(TAG, "Already in connecting state, please wait...");
        return;
    }

    // Check if the address is the initial value (all zeros)
    // 检查地址是否为初始值（全0）
    bool is_valid = false;
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
        if (addr[i] != 0) {
            is_valid = true;
            break;
        }
    }

    if (!is_valid) {
        ESP_LOGE(TAG, "Invalid device address (all zeros)");
        return;
    }

    s_connecting = true;
    ESP_LOGI(TAG, "Try to connect target device name = %s", s_remote_device_name);

    // Do not call lightly, if you connect to a non-existent device address, you will have to wait a while before you can connect again
    // 不要轻易调用，如果连接不存在的设备地址会等待一段时间后才能再次连接
    esp_ble_gattc_open(s_ble_profile.gattc_if,
                       addr,
                       BLE_ADDR_TYPE_PUBLIC,
                       true);
}

/**
 * @brief Reconnect to the last connected device
 * 重新连接到上一次连接的设备
 * 
 * @note Only applicable to non-active disconnection situations, as device information is not cleared
 *       仅适用于非主动断开连接的情况，因为设备信息未被清除
 * @return esp_err_t
 */
esp_err_t ble_reconnect(void) {
    // Check if there is a valid last connection address
    // 检查是否有有效的上一次连接地址
    bool is_valid = false;
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
        if (best_addr[i] != 0) {
            is_valid = true;
            break;
        }
    }

    if (!is_valid) {
        ESP_LOGE(TAG, "No valid previous device address found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Attempting to reconnect to previous device: %s", s_remote_device_name);
    
    // Set reconnection mode flag
    // 设置重连模式标记
    s_is_reconnecting = true;
    s_found_previous_device = false;  // Reset discovery flag
    
    // Start scan task
    // 开始扫描任务
    trigger_scan_task();
    
    return ESP_OK;
}

/**
 * @brief Disconnect (if connected)
 * 断开连接（如果已经连接）
 *
 * @return esp_err_t
 */
esp_err_t ble_disconnect(void) {
    if (s_ble_profile.connection_status.is_connected) {
        esp_ble_gattc_close(s_ble_profile.gattc_if, s_ble_profile.conn_id);
    }
    return ESP_OK;
}

/* -------------------------
 *  Read/Write and Notify related interfaces
 *  读写与 Notify 相关接口
 * ------------------------- */
/**
 * @brief Read a specified characteristic
 * 读取指定特征
 *
 * @param conn_id  Connection ID (obtained from callback events or internal management)
 *                 连接 ID（由回调事件或内部管理获得）
 * @param handle   Handle of the characteristic
 *                 特征的 handle
 * @return esp_err_t
 */
esp_err_t ble_read(uint16_t conn_id, uint16_t handle) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip read");
        return ESP_FAIL;
    }
    /* Initiate GATTC read request */
    /* 发起 GATTC 读请求 */
    esp_err_t ret = esp_ble_gattc_read_char(s_ble_profile.gattc_if,
                                            conn_id,
                                            handle,
                                            ESP_GATT_AUTH_REQ_NONE);
    if (ret) {
        ESP_LOGE(TAG, "read_char failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write characteristic (Write Without Response)
 * 写特征（Write Without Response）
 *
 * @param conn_id   Connection ID
 *                  连接 ID
 * @param handle    Handle of the characteristic
 *                  特征 handle
 * @param data      Data to be written
 *                  要写入的数据
 * @param length    Length of the data
 *                  数据长度
 * @return esp_err_t
 */
esp_err_t ble_write_without_response(uint16_t conn_id, uint16_t handle, const uint8_t *data, size_t length) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip write_without_response");
        return ESP_FAIL;
    }
    esp_err_t ret = esp_ble_gattc_write_char(s_ble_profile.gattc_if,
                                             conn_id,
                                             handle,
                                             length,
                                             (uint8_t *)data,
                                             ESP_GATT_WRITE_TYPE_NO_RSP,
                                             ESP_GATT_AUTH_REQ_NONE);
    if (ret) {
        ESP_LOGE(TAG, "write_char NO_RSP failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write characteristic (Write With Response)
 * 写特征（Write With Response）
 *
 * @param conn_id   Connection ID
 *                  连接 ID
 * @param handle    Handle of the characteristic
 *                  特征 handle
 * @param data      Data to be written
 *                  要写入的数据
 * @param length    Length of the data
 *                  数据长度
 * @return esp_err_t
 */
esp_err_t ble_write_with_response(uint16_t conn_id, uint16_t handle, const uint8_t *data, size_t length) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip write_with_response");
        return ESP_FAIL;
    }
    esp_err_t ret = esp_ble_gattc_write_char(s_ble_profile.gattc_if,
                                             conn_id,
                                             handle,
                                             length,
                                             (uint8_t *)data,
                                             ESP_GATT_WRITE_TYPE_RSP,
                                             ESP_GATT_AUTH_REQ_NONE);
    if (ret) {
        ESP_LOGE(TAG, "write_char RSP failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Register (enable) Notify
 * 注册（开启）Notify
 *
 * @param conn_id   Connection ID
 *                  连接 ID
 * @param char_handle Handle of the characteristic to enable notification
 *                    需要开启通知的特征 handle
 * @return esp_err_t
 */
esp_err_t ble_register_notify(uint16_t conn_id, uint16_t char_handle) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip register_notify");
        return ESP_FAIL;
    }
    /* Request to subscribe to notifications from the protocol stack */
    /* 向协议栈请求订阅通知 */
    esp_err_t ret = esp_ble_gattc_register_for_notify(s_ble_profile.gattc_if,
                                                      s_ble_profile.remote_bda,
                                                      char_handle);
    if (ret) {
        ESP_LOGE(TAG, "register_notify failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Unregister (disable) Notify
 * 反注册（关闭）Notify
 *
 * @note  This is just a demonstration logic. You need the Client Config descriptor handle of the characteristic to operate.
 *        If needed in actual projects, you can directly save the descr handle previously, and then close it by writing 0x0000 here.
 *        此处仅示例逻辑，需要特征的 Client Config 描述符 handle 来进行操作
 *        若实际项目需要，也可直接先前保存 descr handle，然后在此进行关闭写 0x0000
 *
 * @param conn_id   Connection ID
 *                  连接 ID
 * @param char_handle Handle of the characteristic to disable notification
 *                    需要关闭通知的特征 handle
 * @return esp_err_t
 */
esp_err_t ble_unregister_notify(uint16_t conn_id, uint16_t char_handle) {
    /* In fact, you need to get the corresponding descriptor handle and then write 0x0000 to disable it */
    /* 实际上需要获取到对应的描述符 handle，然后写 0x0000 进行关闭 */
    /* This is just a demonstration of the process. If needed, you can save the descr handle during register_notify */
    /* 这里只是演示一下流程，需要时可在 register_notify 时保存 descr handle */
    ESP_LOGI(TAG, "ble_unregister_notify called (demo), not fully implemented");
    return ESP_OK;
}

/**
 * @brief Set global Notify callback (for receiving data)
 * 设置全局的 Notify 回调（用于接收数据）
 *
 * @param cb Callback function pointer
 *           回调函数指针
 */
void ble_set_notify_callback(ble_notify_callback_t cb) {
    s_notify_cb = cb;
}

/**
 * @brief Set global logic layer disconnection state callback
 * 设置全局的逻辑层断连状态回调
 *
 * @param cb Callback function pointer
 *           回调函数指针
 */
void ble_set_state_callback(connect_logic_state_callback_t cb) {
    s_state_cb = cb;
}

/* ----------------------------------------------------------------
 *   GAP & GATTC callback function implementation (simplified version)
 *   GAP & GATTC 回调函数实现（精简版）
 * ---------------------------------------------------------------- */

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT");
        trigger_scan_task();
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "scan stopped");
        // After scanning ends, decide whether to connect based on reconnection mode and device discovery status
        // 扫描结束后，根据重连模式和设备发现状态决定是否连接
        if (best_rssi > -128) {
            if (!s_is_reconnecting || (s_is_reconnecting && s_found_previous_device)) {
                ESP_LOGI(TAG, "Connecting to device: %02x:%02x:%02x:%02x:%02x:%02x",
                         best_addr[0], best_addr[1], best_addr[2], best_addr[3], best_addr[4], best_addr[5]);
                try_to_connect(best_addr);
            } else {
                ESP_LOGW(TAG, "In reconnection mode but target device not found");
            }
        } else {
            ESP_LOGW(TAG, "No suitable device found with sufficient signal strength");
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *r = param;
        if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            /* Get the complete name from the advertisement/response data */
            /* 获取广播/响应数据里的完整名称 */
            uint8_t *adv_name = NULL;
            uint8_t adv_name_len = 0;
            adv_name = esp_ble_resolve_adv_data_by_type(r->scan_rst.ble_adv,
                                r->scan_rst.adv_data_len + r->scan_rst.scan_rsp_len,
                                ESP_BLE_AD_TYPE_NAME_CMPL,
                                &adv_name_len);

            /* Compare names and record signal strength */
            /* 对比名称并记录信号强度 */
            if (adv_name && adv_name_len > 0) {
                // Print the name and signal strength of the found device
                // 打印搜索到的设备名称和信号强度
                // ESP_LOGI(TAG, "Found device: %s with RSSI: %d", adv_name, r->scan_rst.rssi);

                // If the device name starts with "Osmo"
                // 如果设备名以"Osmo"开头
                if (strncmp((char *)adv_name, "Osmo", 4) == 0) {
                    if (s_is_reconnecting) {
                        // In reconnection mode, compare device addresses
                        // 在重连模式下，比对设备地址
                        if (memcmp(best_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t)) == 0) {
                            s_found_previous_device = true;
                            ESP_LOGI(TAG, "Found previous device: %s, RSSI: %d", adv_name, r->scan_rst.rssi);
                        }
                    } else {
                        // In normal scan mode, record the device with the strongest signal
                        // 正常扫描模式，记录信号最强的设备
                        if (r->scan_rst.rssi > best_rssi && r->scan_rst.rssi >= MIN_RSSI_THRESHOLD) {
                            best_rssi = r->scan_rst.rssi;
                            memcpy(best_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t));
                            strncpy(s_remote_device_name, (char *)adv_name, sizeof(s_remote_device_name) - 1);
                            s_remote_device_name[sizeof(s_remote_device_name) - 1] = '\0';
                        }
                    }
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
    switch (event) {
    case ESP_GATTC_REG_EVT: {
        // Handle GATT client registration event
        // 处理 GATT 客户端注册事件
        if (param->reg.status == ESP_GATT_OK) {
            s_ble_profile.gattc_if = gattc_if;
            ESP_LOGI(TAG, "GATTC register OK, app_id=%d, gattc_if=%d",
                     param->reg.app_id, gattc_if);
        } else {
            ESP_LOGE(TAG, "GATTC register failed, status=%d", param->reg.status);
        }
        break;
    }
    case ESP_GATTC_CONNECT_EVT: {
        // Handle connection event
        // 处理连接事件
        s_ble_profile.conn_id = param->connect.conn_id;
        s_ble_profile.connection_status.is_connected = true;
        memcpy(s_ble_profile.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "Connected, conn_id=%d", s_ble_profile.conn_id);

        // Initiate MTU request
        // 发起 MTU 请求
        esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
        break;
    }
    case ESP_GATTC_OPEN_EVT: {
        // Handle connection open event
        // 处理连接打开事件
        s_connecting = false;
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Open failed, status=%d", param->open.status);
            break;
        }
        ESP_LOGI(TAG, "Open success, MTU=%u", param->open.mtu);
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT: {
        // Handle MTU configuration event
        // 处理 MTU 配置事件
        if (param->cfg_mtu.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Config MTU Error, status=%d", param->cfg_mtu.status);
        }
        ESP_LOGI(TAG, "MTU=%d", param->cfg_mtu.mtu);

        // Start service discovery after MTU configuration
        // MTU 配置完后开始发现服务
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT: {
        // Handle service search result event
        // 处理服务搜索结果事件
        if ((param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16) &&
            (param->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_TARGET_SERVICE_UUID)) {
            s_ble_profile.service_start_handle = param->search_res.start_handle;
            s_ble_profile.service_end_handle   = param->search_res.end_handle;
            ESP_LOGI(TAG, "Service found: start=%d, end=%d",
                     s_ble_profile.service_start_handle,
                     s_ble_profile.service_end_handle);
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
        // Handle service search complete event
        // 处理服务搜索完成事件
        if (param->search_cmpl.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Service search failed, status=%d", param->search_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Service search complete, next get char by UUID");

        // Get notify characteristic handle
        // 获取通知特征句柄
        uint16_t count = 1;
        esp_gattc_char_elem_t char_elem_result;
        esp_ble_gattc_get_char_by_uuid(gattc_if,
                                       s_ble_profile.conn_id,
                                       s_ble_profile.service_start_handle,
                                       s_ble_profile.service_end_handle,
                                       s_filter_notify_char_uuid,
                                       &char_elem_result,
                                       &count);
        if (count > 0) {
            s_ble_profile.notify_char_handle = char_elem_result.char_handle;
            s_ble_profile.handle_discovery.notify_char_handle_found = true;
            ESP_LOGI(TAG, "Notify Char found, handle=0x%x",
                     s_ble_profile.notify_char_handle);
        }

        // Get write characteristic handle
        // 获取写特征句柄
        count = 1;
        esp_gattc_char_elem_t write_char_elem_result;
        esp_ble_gattc_get_char_by_uuid(gattc_if,
                                       s_ble_profile.conn_id,
                                       s_ble_profile.service_start_handle,
                                       s_ble_profile.service_end_handle,
                                       s_filter_write_char_uuid,
                                       &write_char_elem_result,
                                       &count);
        if (count > 0) {
            s_ble_profile.write_char_handle = write_char_elem_result.char_handle;
            s_ble_profile.handle_discovery.write_char_handle_found = true;
            ESP_LOGI(TAG, "Write Char found, handle=0x%x",
                     s_ble_profile.write_char_handle);
        }

        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        // Handle notification registration event
        // 处理通知注册事件
        if (param->reg_for_notify.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Notify register failed, status=%d", param->reg_for_notify.status);
            break;
        }
        ESP_LOGI(TAG, "Notify register success, handle=0x%x", param->reg_for_notify.handle);

        // Find descriptor and write 0x01 to enable notification
        // 找到对应描述符并写入 0x01 使能通知
        uint16_t count = 1;
        esp_gattc_descr_elem_t descr_elem;
        esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                               s_ble_profile.conn_id,
                                               param->reg_for_notify.handle,
                                               s_notify_descr_uuid,
                                               &descr_elem,
                                               &count);
        if (count > 0 && descr_elem.handle) {
            uint16_t notify_en = 1;
            esp_ble_gattc_write_char_descr(gattc_if,
                                           s_ble_profile.conn_id,
                                           descr_elem.handle,
                                           sizeof(notify_en),
                                           (uint8_t *)&notify_en,
                                           ESP_GATT_WRITE_TYPE_RSP,
                                           ESP_GATT_AUTH_REQ_NONE);
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
        // Handle notification data event
        // 处理通知数据事件
        if (s_notify_cb) {
            s_notify_cb(param->notify.value, param->notify.value_len);
        }
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
        // Handle disconnection event
        // 处理断开连接事件
        s_ble_profile.connection_status.is_connected = false;
        s_ble_profile.handle_discovery.write_char_handle_found = false;
        s_ble_profile.handle_discovery.notify_char_handle_found = false;
        s_connecting = false;
        ESP_LOGI(TAG, "Disconnected, reason=0x%x", param->disconnect.reason);

        if (s_state_cb) {
            s_state_cb();
        }
        break;
    }
    default:
        break;
    }
}
