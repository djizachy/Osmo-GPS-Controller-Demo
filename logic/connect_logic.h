#ifndef __CONNECT_LOGIC_H__
#define __CONNECT_LOGIC_H__

// 定义连接状态枚举类型
typedef enum {
    CONNECT_STATE_DISCONNECTED,       // 断开连接
    CONNECT_STATE_BLE_CONNECTED,      // BLE 已连接
    CONNECT_STATE_PROTOCOL_CONNECTED, // 协议链路已连接
} connect_state_t;

// 获取当前连接状态
connect_state_t connect_logic_get_state(void);

// ble 连接
int connect_logic_ble_connect(const char *camera_name);

// 断开 ble 连接
int connect_logic_ble_disconnect(void);

// 协议连接
int connect_logic_protocol_connect(uint32_t device_id, uint8_t mac_addr_len, const int8_t *mac_addr,
                                    uint32_t fw_version, uint8_t verify_mode, uint16_t verify_data,
                                    uint8_t camera_reserved);

#endif