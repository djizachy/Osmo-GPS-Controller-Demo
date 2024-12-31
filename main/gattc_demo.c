/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/****************************************************************************
 *
 * 该示例展示了 BLE GATT 客户端的功能。它可以扫描 BLE 设备并连接到一个设备。
 * 运行 gatt_server 示例后，客户端示例会自动连接到 gatt_server。
 * 客户端示例在连接后会启用 gatt_server 的通知功能。两个设备随后将交换数据。
 *
 ****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "custom_crc16.h"
#include "custom_crc32.h"

#include "dji_protocol_parser.h"
#include "dji_protocol_data_processor.h"

#define GATTC_TAG "GATTC_DEMO"  // 日志标签
#define REMOTE_SERVICE_UUID        0xFFF0  // 远程服务的 UUID
#define REMOTE_NOTIFY_CHAR_UUID    0xFFF4  // 相机发送，遥控器接收（NOTIFY）
#define REMOTE_WRITE_CHAR_UUID    0xFFF5  // 遥控器发送，相机接收（WRITE, WRITE NO RESPONSE）
#define PROFILE_NUM      1  // 定义配置文件数量
#define PROFILE_A_APP_ID 0  // 配置文件 A 的应用 ID
#define INVALID_HANDLE   0  // 无效的句柄

static char remote_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = "OsmoAction5Pro0C31";  // 远程设备名称
static bool connect    = false;  // 是否已连接
static bool get_server = false;  // 是否获取到服务器
static esp_gattc_char_elem_t *char_elem_result   = NULL;  // 特征元素结果
static esp_gattc_descr_elem_t *descr_elem_result = NULL;  // 描述元素结果

/* 声明静态函数 */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,  // UUID 长度为 16 位
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

static esp_bt_uuid_t remote_filter_notify_char_uuid = {
    .len = ESP_UUID_LEN_16,  // UUID 长度为 16 位
    .uuid = {.uuid16 = REMOTE_NOTIFY_CHAR_UUID},  // 填入接收特征的 UUID
};

static esp_bt_uuid_t remote_filter_write_char_uuid = {
    .len = ESP_UUID_LEN_16,  // UUID 长度为 16 位
    .uuid = {.uuid16 = REMOTE_WRITE_CHAR_UUID},  // 填入发送特征的 UUID
};

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,  // UUID 长度为 16 位
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,  // 主动扫描
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,  // 公共地址类型
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,  // 扫描过滤策略，允许所有
    .scan_interval          = 0x50,  // 扫描间隔
    .scan_window            = 0x30,  // 扫描窗口
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE  // 禁用重复扫描
};

/* GATT 客户端配置文件结构体 */
struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;  // GATT 客户端回调函数
    uint16_t gattc_if;  // GATT 客户端接口
    uint16_t app_id;  // 应用 ID
    uint16_t conn_id;  // 连接 ID
    uint16_t service_start_handle;  // 服务起始句柄
    uint16_t service_end_handle;  // 服务结束句柄
    uint16_t notify_char_handle;  // NOTIFY 特征句柄
    uint16_t write_char_handle;  // WRITE 特征句柄
    esp_bd_addr_t remote_bda;  // 远程设备地址
};

/* 定义配置文件数组，每个配置文件对应一个 app_id 和 gattc_if */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,  // 配置文件 A 的回调函数
        .gattc_if = ESP_GATT_IF_NONE,  // 初始化为 ESP_GATT_IF_NONE
    },
};

/* GATT 客户端配置文件事件处理函数 */
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:  // 注册事件
        ESP_LOGI(GATTC_TAG, "GATT client register, status %d, app_id %d, gattc_if %d", param->reg.status, param->reg.app_id, gattc_if);
        // 设置扫描参数
        esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
        }
        break;
    case ESP_GATTC_CONNECT_EVT:{  // 连接事件
        ESP_LOGI(GATTC_TAG, "Connected, conn_id %d, remote "ESP_BD_ADDR_STR"", p_data->connect.conn_id,
                 ESP_BD_ADDR_HEX(p_data->connect.remote_bda));
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->connect.conn_id;  // 保存连接 ID
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));  // 保存远程设备地址
        // 发送 MTU 请求
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->connect.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "Config MTU error, error code = %x", mtu_ret);
        }
        break;
    }
    case ESP_GATTC_OPEN_EVT:  // 打开事件
        if (param->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Open failed, status %d", p_data->open.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Open successfully, MTU %u", p_data->open.mtu);
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:  // 服务发现完成事件
        if (param->dis_srvc_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Service discover failed, status %d", param->dis_srvc_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Service discover complete, conn_id %d", param->dis_srvc_cmpl.conn_id);
        // 搜索特定服务
        esp_ble_gattc_search_service(gattc_if, param->dis_srvc_cmpl.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_CFG_MTU_EVT:  // MTU 配置事件
        ESP_LOGI(GATTC_TAG, "MTU exchange, status %d, MTU %d", param->cfg_mtu.status, param->cfg_mtu.mtu);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {  // 服务搜索结果事件
        ESP_LOGI(GATTC_TAG, "Service search result, conn_id = %x, is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(GATTC_TAG, "start handle %d, end handle %d, current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        // 检查是否找到目标服务
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
            ESP_LOGI(GATTC_TAG, "Service found");
            get_server = true;  // 标记已获取服务器
            gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:  // 服务搜索完成事件
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Service search failed, status %x", p_data->search_cmpl.status);
            break;
        }
        // 根据服务信息来源进行日志记录
        if(p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
            ESP_LOGI(GATTC_TAG, "Get service information from remote device");
        } else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH) {
            ESP_LOGI(GATTC_TAG, "Get service information from flash");
        } else {
            ESP_LOGI(GATTC_TAG, "Unknown service source");
        }
        ESP_LOGI(GATTC_TAG, "Service search complete");
        if (get_server){
            uint16_t count = 0;
            // 获取特征数量
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     p_data->search_cmpl.conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                     INVALID_HANDLE,
                                                                     &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
                break;
            }

            if (count > 0){
                // 分配内存以存储特征信息
                char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (!char_elem_result){
                    ESP_LOGE(GATTC_TAG, "gattc no mem");
                    break;
                }else{
                    /***** 查找 NOTIFY 特征 ******/
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                             p_data->search_cmpl.conn_id,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                             remote_filter_notify_char_uuid,
                                                             char_elem_result,
                                                             &count);
                    if (status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_notify_char_by_uuid error");
                        free(char_elem_result);
                        char_elem_result = NULL;
                        break;
                    }

                    /* 每个服务在我们的 'ESP_GATTS_DEMO' 示例中只有一个特征，因此使用第一个 'char_elem_result' */
                    if (count > 0 && (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)){
                        gl_profile_tab[PROFILE_A_APP_ID].notify_char_handle = char_elem_result[0].char_handle;
                        // 注册 NOTIFY 特征
                        esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, char_elem_result[0].char_handle);
                    }

                    /***** 查找 WRITE 特征 ******/
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                             p_data->search_cmpl.conn_id,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                             remote_filter_write_char_uuid,
                                                             char_elem_result,
                                                             &count);
                    if (status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_write_char_by_uuid error");
                        free(char_elem_result);
                        char_elem_result = NULL;
                        break;
                    }

                    if (count > 0 && (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)){
                        gl_profile_tab[PROFILE_A_APP_ID].write_char_handle = char_elem_result[0].char_handle;
                        // WRITE 特征不需要注册，需保存特征句柄用于后续写入操作
                    }
                    
                }
                /* 释放 char_elem_result */
                free(char_elem_result);
            }else{
                ESP_LOGE(GATTC_TAG, "no char found");
            }
        }
         break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {  // 注册通知事件
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Notification register failed, status %d", p_data->reg_for_notify.status);
        }else{
            ESP_LOGI(GATTC_TAG, "Notification register successfully");
            uint16_t count = 0;
            uint16_t notify_en = 1;  // 通知使能
            // 获取描述符数量
            esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                         ESP_GATT_DB_DESCRIPTOR,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].notify_char_handle,
                                                                         &count);
            if (ret_status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
                break;
            }
            if (count > 0){
                // 分配内存以存储描述符信息
                descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
                if (!descr_elem_result){
                    ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
                    break;
                }else{
                    // 根据描述符 UUID 获取描述符
                    ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                         p_data->reg_for_notify.handle,
                                                                         notify_descr_uuid,
                                                                         descr_elem_result,
                                                                         &count);
                    if (ret_status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                        free(descr_elem_result);
                        descr_elem_result = NULL;
                        break;
                    }
                    /* 每个特征在我们的 'ESP_GATTS_DEMO' 示例中只有一个描述符，因此使用第一个 'descr_elem_result' */
                    if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG){
                        // 写入描述符以启用通知
                        ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                     descr_elem_result[0].handle,
                                                                     sizeof(notify_en),
                                                                     (uint8_t *)&notify_en,
                                                                     ESP_GATT_WRITE_TYPE_RSP,
                                                                     ESP_GATT_AUTH_REQ_NONE);
                    }

                    if (ret_status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                    }

                    /* 释放 descr_elem_result */
                    free(descr_elem_result);
                }
            }
            else{
                ESP_LOGE(GATTC_TAG, "decsr not found");
            }

        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:  // 通知事件
        // 检查帧头是否为 0xAA
        if (p_data->notify.value_len >= 2 && 
            (p_data->notify.value[0] == 0xAA || p_data->notify.value[0] == 0xaa)) {

            ESP_LOGI(GATTC_TAG, "Notification received, attempting to parse...");
            ESP_LOG_BUFFER_HEX(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);  // 打印通知内容

            // 定义解析结果结构体
            protocol_frame_t frame;

            // 调用 protocol_parse_notification 解析通知帧
            uint16_t expected_seq = 0x1234;
            int ret = protocol_parse_notification(p_data->notify.value, p_data->notify.value_len, expected_seq, &frame);
            if (ret != 0) {
                ESP_LOGE(GATTC_TAG, "Failed to parse notification frame, error: %d", ret);
                break;  // 停止处理
            }

            // 调用 protocol_parse_data 解析 data 数据段
            if (frame.data != NULL && frame.data_length > 0) {
                ret = protocol_parse_data(frame.data, frame.data_length);
                if (ret != 0) {
                    ESP_LOGE(GATTC_TAG, "Failed to parse data segment, error: %d", ret);
                }
            } else {
                ESP_LOGW(GATTC_TAG, "Data segment is empty, skipping data parsing");
            }
        } else {
            // ESP_LOGW(GATTC_TAG, "Received frame does not start with 0xAA, ignoring...");
        }
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:  // 写描述符事件
        if (p_data->write.status != ESP_GATT_OK) {
            ESP_LOGE(GATTC_TAG, "Descriptor write failed, status %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Descriptor write successfully");

        // 切换摄影模式示例
        // uint8_t data[] = {0x1D, 0x04, 0x33, 0xFF, 0x00, 0x00, 0x0A, 0x01, 0x47, 0x39, 0x36, 0x37}; // 示例 DATA 数据
        // size_t data_len = sizeof(data);
        // size_t frame_length_out = 0; // 用于存储帧的实际长度
        // uint8_t *frame = create_version_query_frame(data, data_len, &frame_length_out);

        // 查询版本号示例
        // uint8_t data[] = {0x00, 0x00};
        // size_t data_len = sizeof(data);
        // size_t frame_length_out = 0;
        // uint8_t *frame = create_version_query_frame(data, data_len, &frame_length_out);

        // 直接构造帧
        // size_t frame_length_out = 28;
        // uint8_t temp_frame[28] = {0xAA, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x3A, 0xC8, 0x6D, 0x1D, 0x04, 0x33, 0xFF, 0x00, 0x00, 0x0A, 0x01, 0x47, 0x39, 0x36, 0x37, 0x9C, 0x18, 0x9D, 0x03};
        // uint8_t *frame = temp_frame;

        // 分层测试
        // 构造 key-value 数据
        key_value_t key_values[] = {
            {"device_id", (void *)"\x33\xFF\x00\x00", 4},  // 设备 ID 字段
            {"mode", (void *)"\x0A", 1},                  // 模式字段
            {"reserved", (void *)"\x01\x47\x39\x36", 4}   // 预留字段
        };
        
        // 准备调用协议帧创建函数
        uint8_t cmd_set = 0x1D;       // 示例 CmdSet
        uint8_t cmd_id = 0x04;        // 示例 CmdID
        uint8_t cmd_type = 0x01;      // 示例命令类型
        uint16_t seq = 0x1234;        // 示例序列号
        size_t frame_length_out = 0;  // 存储生成帧的实际长度

        // 调用协议帧创建函数
        uint8_t *frame = protocol_create_frame(cmd_set, cmd_id, cmd_type, key_values, 3, seq, &frame_length_out);
        if (frame == NULL) {
            ESP_LOGE(GATTC_TAG, "Failed to create protocol frame");
            break;
        }
        ESP_LOGI(GATTC_TAG, "Protocol frame created successfully, length: %zu", frame_length_out);

        if (frame) {
            // 打印 ByteArray 格式
            printf("ByteArray: [");
            for (size_t i = 0; i < frame_length_out; i++) {
                printf("%02X", frame[i]);
                if (i < frame_length_out - 1) {
                    printf(", ");
                }
            }
            printf("]\n");
        }

        // 发送数据帧到特征
        esp_ble_gattc_write_char(
            gattc_if,
            gl_profile_tab[PROFILE_A_APP_ID].conn_id,
            gl_profile_tab[PROFILE_A_APP_ID].write_char_handle,
            frame_length_out,
            frame,
            ESP_GATT_WRITE_TYPE_RSP,   // 响应写入
            ESP_GATT_AUTH_REQ_NONE);   // 无认证要求
        
        // free(frame);
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {  // 服务变化事件
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "Service change from "ESP_BD_ADDR_STR"", ESP_BD_ADDR_HEX(bda));
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:  // 写特征事件
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Characteristic write failed, status %x)", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Characteristic write successfully");
        break;
    case ESP_GATTC_DISCONNECT_EVT:  // 断开连接事件
        connect = false;
        get_server = false;
        ESP_LOGI(GATTC_TAG, "Disconnected, remote "ESP_BD_ADDR_STR", reason 0x%02x",
                 ESP_BD_ADDR_HEX(p_data->disconnect.remote_bda), p_data->disconnect.reason);
        break;
    default:
        break;
    }
}

/* GAP 回调函数，用于处理扫描和连接相关事件 */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;  // 广播名称指针
    uint8_t adv_name_len = 0;  // 广播名称长度
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {  // 扫描参数设置完成事件
        // 设置扫描持续时间，单位为秒
        uint32_t duration= 30;
        esp_ble_gap_start_scanning(duration);  // 开始扫描
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:  // 扫描开始完成事件
        // 扫描开始完成事件，用于指示扫描是否成功启动
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "Scanning start failed, status %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Scanning start successfully");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {  // 扫描结果事件
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:  // 扫描到一个设备
            // 解析广播数据中的完整设备名称
            adv_name = esp_ble_resolve_adv_data_by_type(scan_result->scan_rst.ble_adv,
                                                        scan_result->scan_rst.adv_data_len + scan_result->scan_rst.scan_rsp_len,
                                                        ESP_BLE_AD_TYPE_NAME_CMPL,
                                                        &adv_name_len);
            // 打印扫描到的设备
            // ESP_LOGI(GATTC_TAG, "Scan result, device "ESP_BD_ADDR_STR", name len %u", ESP_BD_ADDR_HEX(scan_result->scan_rst.bda), adv_name_len);
            // ESP_LOG_BUFFER_CHAR(GATTC_TAG, adv_name, adv_name_len);

#if CONFIG_EXAMPLE_DUMP_ADV_DATA_AND_SCAN_RESP
            if (scan_result->scan_rst.adv_data_len > 0) {
                ESP_LOGI(GATTC_TAG, "adv data:");
                ESP_LOG_BUFFER_HEX(GATTC_TAG, &scan_result->scan_rst.ble_adv[0], scan_result->scan_rst.adv_data_len);
            }
            if (scan_result->scan_rst.scan_rsp_len > 0) {
                ESP_LOGI(GATTC_TAG, "scan resp:");
                ESP_LOG_BUFFER_HEX(GATTC_TAG, &scan_result->scan_rst.ble_adv[scan_result->scan_rst.adv_data_len], scan_result->scan_rst.scan_rsp_len);
            }
#endif

            if (adv_name != NULL) {
                // 检查扫描到的设备名称是否与目标设备名称匹配
                if (strlen(remote_device_name) == adv_name_len && strncmp((char *)adv_name, remote_device_name, adv_name_len) == 0) {
                    // 注意：如果有多个设备具有相同的设备名称，可能会连接到意外的设备。
                    // 建议更改默认设备名称以确保其唯一性。
                    ESP_LOGI(GATTC_TAG, "Device found %s", remote_device_name);
                    if (connect == false) {
                        connect = true;  // 标记为正在连接
                        ESP_LOGI(GATTC_TAG, "Connect to the remote device");
                        esp_ble_gap_stop_scanning();  // 停止扫描
                        esp_ble_gatt_creat_conn_params_t creat_conn_params = {0};
                        memcpy(&creat_conn_params.remote_bda, scan_result->scan_rst.bda, ESP_BD_ADDR_LEN);
                        creat_conn_params.remote_addr_type = scan_result->scan_rst.ble_addr_type;
                        creat_conn_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
                        creat_conn_params.is_direct = true;  // 直接连接
                        creat_conn_params.is_aux = false;
                        creat_conn_params.phy_mask = 0x0;
                        // 创建连接
                        esp_ble_gattc_enh_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                                            &creat_conn_params);
                    }
                }
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:  // 扫描查询完成事件
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:  // 扫描停止完成事件
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Scanning stop failed, status %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Scanning stop successfully");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:  // 广播停止完成事件
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Advertising stop failed, status %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:  // 连接参数更新事件
         ESP_LOGI(GATTC_TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                  param->update_conn_params.status,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:  // 数据包长度设置完成事件
        ESP_LOGI(GATTC_TAG, "Packet length update, status %d, rx %d, tx %d",
                  param->pkt_data_length_cmpl.status,
                  param->pkt_data_length_cmpl.params.rx_len,
                  param->pkt_data_length_cmpl.params.tx_len);
        break;
    default:
        break;
    }
}

/* GATT 客户端回调函数，分发事件到相应的配置文件处理函数 */
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* 如果是注册事件，存储每个配置文件的 gattc_if */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* 如果 gattc_if 等于配置文件 A，调用配置文件 A 的回调处理函数，
     * 因此这里需要调用每个配置文件的回调 */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE 表示未指定特定的 gatt_if，需要调用每个配置文件的回调函数 */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);  // 调用配置文件的回调函数
                }
            }
        }
    } while (0);
}

/* 应用程序的主函数 */
void app_main(void)
{
    // 初始化 NVS（非易失性存储）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    #if CONFIG_EXAMPLE_CI_PIPELINE_ID
    memcpy(remote_device_name, esp_bluedroid_get_example_name(), sizeof(remote_device_name));
    #endif

    // 释放经典蓝牙内存，因为只使用 BLE
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // 配置蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // 启用蓝牙控制器的 BLE 模式
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // 初始化 Bluedroid 堆栈
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // 启用 Bluedroid
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // 注册 GAP 回调函数
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gap register failed, error code = %x", __func__, ret);
        return;
    }

    // 注册 GATTC 回调函数
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "%s gattc register failed, error code = %x", __func__, ret);
        return;
    }

    // 注册 GATTC 应用
    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x", __func__, ret);
    }

    // 设置本地 MTU
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTC_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }


    /*
    * 这段代码用于调试并打印所有 HCI 数据。
    * 要启用它，请打开 "BT_HCI_LOG_DEBUG_EN" 配置选项。
    * 输出的 HCI 数据可以使用脚本解析：
    * esp-idf/tools/bt/bt_hci_to_btsnoop.py.
    * 有关详细说明，请参阅 esp-idf/tools/bt/README.md.
    */

    /*
    while (1) {
        extern void bt_hci_log_hci_data_show(void);
        extern void bt_hci_log_hci_adv_show(void);
        bt_hci_log_hci_data_show();
        bt_hci_log_hci_adv_show();
        vTaskDelay(1000 / portNUM_PROCESSORS);
    }
    */

}
