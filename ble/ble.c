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

/* 目标设备名称 */
static char s_remote_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = {0};

/* 是否已发起连接、是否已找到目标服务等标记，仅作演示 */
static bool s_connecting = false;

/* 全局保存的 Notify 回调 */
static ble_notify_callback_t s_notify_cb = NULL;

/* 设置逻辑层断开连接状态回调 */
static connect_logic_state_callback_t s_state_cb = NULL;

/* 扫描到目标设备，尝试连接 */
#define MIN_RSSI_THRESHOLD -80          // 设置最低信号强度阈值，根据需要调整
static esp_bd_addr_t best_addr = {0};   // 存储信号最强设备的地址
static int8_t best_rssi = -128;         // 存储信号最强设备的 RSSI 值，初始化为最弱的信号强度
static bool s_is_reconnecting = false;  // 是否在重连模式中
static bool s_found_previous_device = false;  // 在重连模式中是否找到了原来的设备

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

/* 扫描参数，可根据需求调整 */
static esp_ble_scan_params_t s_ble_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
};

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
    // 启动定时器，在3秒后停止扫描
    scan_timer = xTimerCreate("scan_timer", pdMS_TO_TICKS(3000), pdFALSE, (void *)0, scan_stop_timer_callback);
    if (scan_timer != NULL) {
        xTimerStart(scan_timer, 0);
    }
}

/* -------------------------
 *  初始化/扫描/连接相关接口
 * ------------------------- */

/**
 * @brief BLE 客户端初始化
 *
 * @return esp_err_t
 *         - ESP_OK on success
 *         - Others on failure
 */
esp_err_t ble_init() {
    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 释放经典蓝牙内存 */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* 配置并初始化蓝牙控制器 */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 启动 BLE 控制器 */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始化 Bluedroid 堆栈 */
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "init bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 启用 Bluedroid */
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 注册 GAP 回调 */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gap register error, err code = %x", ret);
        return ret;
    }

    /* 注册 GATTC 回调 */
    ret = esp_ble_gattc_register_callback(gattc_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gattc register error, err code = %x", ret);
        return ret;
    }

    /* 注册 GATTC 应用（此处只有一个 profile，app_id = 0） */
    ret = esp_ble_gattc_app_register(0);
    if (ret) {
        ESP_LOGE(TAG, "gattc app register error, err code = %x", ret);
        return ret;
    }

    /* 设置本地 MTU（可选） */
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "ble_init success!");
    return ESP_OK;
}

/**
 * @brief 连接到指定名称的设备（若已在扫描中，会自动在扫描到该设备时连接）
 *
 * @note  本接口仅作为演示，如果想主动指定地址连接，可自行扩展接口
 * @return esp_err_t
 */
esp_err_t ble_start_scanning_and_connect(void) {
    /* 重置扫描相关变量 */
    memset(best_addr, 0, sizeof(esp_bd_addr_t));
    best_rssi = -128;
    memset(s_remote_device_name, 0, ESP_BLE_ADV_NAME_LEN_MAX);
    s_is_reconnecting = false;
    s_found_previous_device = false;

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
    // 检查是否正在连接中
    if (s_connecting) {
        ESP_LOGW(TAG, "Already in connecting state, please wait...");
        return;
    }

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

    // 不要轻易调用，如果连接不存在的设备地址会等待一段时间后才能再次连接
    esp_ble_gattc_open(s_ble_profile.gattc_if,
                       addr,
                       BLE_ADDR_TYPE_PUBLIC,
                       true);
}

/**
 * @brief 重新连接到上一次连接的设备
 * 
 * @note 仅适用于非主动断开连接的情况，因为设备信息未被清除
 * @return esp_err_t
 */
esp_err_t ble_reconnect(void) {
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
    
    // 设置重连模式标记
    s_is_reconnecting = true;
    s_found_previous_device = false;  // 重置发现标记
    
    // 开始扫描任务
    trigger_scan_task();
    
    return ESP_OK;
}

/**
 * @brief 断开连接（如果已经连接）
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
 *  读写与 Notify 相关接口
 * ------------------------- */
/**
 * @brief 读取指定特征
 *
 * @param conn_id  连接 ID（由回调事件或内部管理获得）
 * @param handle   特征的 handle
 * @return esp_err_t
 */
esp_err_t ble_read(uint16_t conn_id, uint16_t handle) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip read");
        return ESP_FAIL;
    }
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
 * @brief 写特征（Write Without Response）
 *
 * @param conn_id   连接 ID
 * @param handle    特征 handle
 * @param data      要写入的数据
 * @param length    数据长度
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
 * @brief 写特征（Write With Response）
 *
 * @param conn_id   连接 ID
 * @param handle    特征 handle
 * @param data      要写入的数据
 * @param length    数据长度
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
 * @brief 注册（开启）Notify
 *
 * @param conn_id   连接 ID
 * @param char_handle 需要开启通知的特征 handle
 * @return esp_err_t
 */
esp_err_t ble_register_notify(uint16_t conn_id, uint16_t char_handle) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip register_notify");
        return ESP_FAIL;
    }
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
 * @brief 反注册（关闭）Notify
 *
 * @note  此处仅示例逻辑，需要特征的 Client Config 描述符 handle 来进行操作
 *        若实际项目需要，也可直接先前保存 descr handle，然后在此进行关闭写 0x0000
 *
 * @param conn_id   连接 ID
 * @param char_handle 需要关闭通知的特征 handle
 * @return esp_err_t
 */
esp_err_t ble_unregister_notify(uint16_t conn_id, uint16_t char_handle) {
    /* 实际上需要获取到对应的描述符 handle，然后写 0x0000 进行关闭 */
    /* 这里只是演示一下流程，需要时可在 register_notify 时保存 descr handle */
    ESP_LOGI(TAG, "ble_unregister_notify called (demo), not fully implemented");
    return ESP_OK;
}

/**
 * @brief 设置全局的 Notify 回调（用于接收数据）
 *
 * @param cb 回调函数指针
 */
void ble_set_notify_callback(ble_notify_callback_t cb) {
    s_notify_cb = cb;
}

/**
 * @brief 设置全局的逻辑层断连状态回调
 *
 * @param cb 回调函数指针
 */
void ble_set_state_callback(connect_logic_state_callback_t cb) {
    s_state_cb = cb;
}

/* ----------------------------------------------------------------
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
            /* 获取广播/响应数据里的完整名称 */
            uint8_t *adv_name = NULL;
            uint8_t adv_name_len = 0;
            adv_name = esp_ble_resolve_adv_data_by_type(r->scan_rst.ble_adv,
                                r->scan_rst.adv_data_len + r->scan_rst.scan_rsp_len,
                                ESP_BLE_AD_TYPE_NAME_CMPL,
                                &adv_name_len);

            /* 对比名称并记录信号强度 */
            if (adv_name && adv_name_len > 0) {
                // 打印搜索到的设备名称和信号强度
                // ESP_LOGI(TAG, "Found device: %s with RSSI: %d", adv_name, r->scan_rst.rssi);

                // 如果设备名以"OSMO"开头
                if (strncmp((char *)adv_name, "Osmo", 4) == 0) {
                    if (s_is_reconnecting) {
                        // 在重连模式下，比对设备地址
                        if (memcmp(best_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t)) == 0) {
                            s_found_previous_device = true;
                            ESP_LOGI(TAG, "Found previous device: %s, RSSI: %d", adv_name, r->scan_rst.rssi);
                        }
                    } else {
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
        s_ble_profile.conn_id = param->connect.conn_id;
        s_ble_profile.connection_status.is_connected = true;
        memcpy(s_ble_profile.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "Connected, conn_id=%d", s_ble_profile.conn_id);

        /* 发起 MTU 请求 */
        esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
        break;
    }
    case ESP_GATTC_OPEN_EVT: {
        s_connecting = false;  // 重置连接状态
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Open failed, status=%d", param->open.status);
            break;
        }
        ESP_LOGI(TAG, "Open success, MTU=%u", param->open.mtu);
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT: {
        if (param->cfg_mtu.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Config MTU Error, status=%d", param->cfg_mtu.status);
        }
        ESP_LOGI(TAG, "MTU=%d", param->cfg_mtu.mtu);

        /* MTU 配置完后开始发现服务 */
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT: {
        /* 找到一个 Service */
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
        if (param->search_cmpl.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Service search failed, status=%d", param->search_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Service search complete, next get char by UUID");

        /* 获取 notify char handle */
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

        /* 获取 write char handle */
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
        if (param->reg_for_notify.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Notify register failed, status=%d", param->reg_for_notify.status);
            break;
        }
        ESP_LOGI(TAG, "Notify register success, handle=0x%x", param->reg_for_notify.handle);

        /* 找到对应描述符并写入 0x01 使能通知 */
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
        /* 收到远端发来的 Notify 数据 */
        // ESP_LOGI(TAG, "Notify data, len=%d", param->notify.value_len);
        // ESP_LOG_BUFFER_HEX(TAG, param->notify.value, param->notify.value_len);

        /* 如果有上层注册了回调，则直接把数据抛给上层 */
        if (s_notify_cb) {
            s_notify_cb(param->notify.value, param->notify.value_len);
        }
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
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
