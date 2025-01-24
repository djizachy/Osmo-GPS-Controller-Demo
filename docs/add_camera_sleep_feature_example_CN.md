# 添加相机休眠功能-示例文档

本文档的目标：在此开源项目的基础上，实现单击 BOOT 按键让相机休眠。

首先，我们可以从文档中拿到对应的命令帧和应答帧结构信息，如下所示：

```
相机电源模式设置

（CmdSet = 0x00 CmdID = 0x1A），数据段细节如下所示：

| 帧类型   | 偏移 | 大小 |   名字      |   类型   | 描述                              |
|---------|-----|------|------------|---------|----------------------------------|
| 命令帧   | 0   | 1    | power mode | uint8_t | 电源模式设置：0：正常模式，3：休眠模式 |
| 应答帧   | 0   | 1    | ret code   | uint8_t | 参考普通通返回码                    |
```

## 协议层修改

首先，我们需要在 `dji_protocol_data_structures.h` 中定义命令帧和应答帧：

```c
typedef struct __attribute__((packed)) {
    uint8_t power_mode;            // 电源模式：0表示正常模式，3表示休眠模式
} camera_power_mode_switch_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // 返回码：0表示切换成功，非0表示切换失败
} camera_power_mode_switch_response_frame_t;
```

然后，在 `dji_protocol_data_descriptors.c` 中定义相应的 `creator` 和 `parser` 函数：

```c
uint8_t* camera_power_mode_switch_creator(const void *structure, size_t *data_length, uint8_t cmd_type) {
    if (structure == NULL || data_length == NULL) {
        ESP_LOGE(TAG, "Invalid input: structure or data_length is NULL");
        return NULL;
    }

    uint8_t *data = NULL;

    // 判断是否为命令帧
    if ((cmd_type & 0x20) == 0) {
        const camera_power_mode_switch_command_frame_t *command_frame = 
            (const camera_power_mode_switch_command_frame_t *)structure;

        *data_length = sizeof(camera_power_mode_switch_command_frame_t);

        ESP_LOGI(TAG, "Data length calculated for camera_power_mode_switch_command_frame: %zu", *data_length);

        data = (uint8_t *)malloc(*data_length);
        if (data == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed in camera_power_mode_switch_creator");
            return NULL;
        }

        ESP_LOGI(TAG, "Memory allocation succeeded for command frame, copying data...");

        memcpy(data, command_frame, *data_length);
    } else {
        // 暂不支持此功能的应答帧创建
        ESP_LOGE(TAG, "Response frames are not supported in camera_power_mode_switch_creator");
        return NULL;
    }

    return data;
}

int camera_power_mode_switch_parser(const uint8_t *data, size_t data_length, void *structure_out, uint8_t cmd_type) {
    if (data == NULL || structure_out == NULL) {
        ESP_LOGE(TAG, "camera_power_mode_switch_parser: NULL input detected");
        return -1;
    }

    ESP_LOGI(TAG, "Parsing Camera Power Mode Switch data, received data length: %zu", data_length);

    if ((cmd_type & 0x20) == 0) {
        // 暂不支持此功能的命令帧解析
        ESP_LOGE(TAG, "camera_power_mode_switch_parser: Only response frames are supported");
        return -1;
    }

    if (data_length < sizeof(camera_power_mode_switch_response_frame_t)) {
        ESP_LOGE(TAG, "camera_power_mode_switch_parser: Data length too short for response frame. Expected: %zu, Got: %zu",
                 sizeof(camera_power_mode_switch_response_frame_t), data_length);
        return -1;
    }

    const camera_power_mode_switch_response_frame_t *response = (const camera_power_mode_switch_response_frame_t *)data;
    camera_power_mode_switch_response_frame_t *output_response = (camera_power_mode_switch_response_frame_t *)structure_out;

    output_response->ret_code = response->ret_code;

    ESP_LOGI(TAG, "Camera Power Mode Switch Response parsed successfully. ret_code: %u", output_response->ret_code);

    return 0;
}
```

接下来，在 `dji_protocol_data_descriptors.h` 中声明 `creator` 和 `parser` 函数：

```c
uint8_t* camera_power_mode_switch_creator(const void *structure, size_t *data_length, uint8_t cmd_type);
int camera_power_mode_switch_parser(const uint8_t *data, size_t data_length, void *structure_out, uint8_t cmd_type);
```

最后，在 `dji_protocol_data_descriptors.c` 的数组中加入相应的三元组：

```c
const data_descriptor_t data_descriptors[] = {
    // 相机电源模式切换
    {0x00, 0x1A, (data_creator_func_t)camera_power_mode_switch_creator, (data_parser_func_t)camera_power_mode_switch_parser},
};
```

## 逻辑层创建函数

在 `command_logic.c` 中新建函数：

```c
camera_power_mode_switch_response_frame_t* command_logic_power_mode_switch_sleep(void) {
    // 打印日志，表示正在上报电源模式切换到休眠模式
    ESP_LOGI(TAG, "%s: Reporting power mode switch to sleep", __FUNCTION__);

    // 检查协议是否连接
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();  // 生成序列号

    // 构造命令帧，设置电源模式为休眠模式（0x03）
    camera_power_mode_switch_command_frame_t command_frame = {
        .power_mode = 0x03,        // 设置为 0x03 表示休眠模式
    };

    // 发送命令并接收响应
    CommandResult result = send_command(
        0x00,
        0x1A,                 // 使用 CmdSet = 0x00 和 CmdID = 0x1A 表示电源模式切换
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    // 如果没有返回结构体，表示发送或接收失败
    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    // 将返回的数据结构转换为对应的响应帧
    camera_power_mode_switch_response_frame_t *response = (camera_power_mode_switch_response_frame_t *)result.structure;

    // 打印返回的电源模式切换响应信息
    ESP_LOGI(TAG, "Power Mode Switch Response: ret_code=%d", response->ret_code);

    return response;  // 返回解析后的响应帧
}
```

在 `command_logic.h` 中声明：

```c
camera_power_mode_switch_response_frame_t* command_logic_power_mode_switch_sleep(void);
```

## 修改按键逻辑

在 `key_logic.c` 中，修改 `handle_boot_single_press` 函数为：

```c
static void handle_boot_single_press() {
    // 切换到休眠模式
    // Switch to sleep mode
    camera_power_mode_switch_response_frame_t *response = command_logic_power_mode_switch_sleep();

    if (response != NULL) {
        // 打印日志，输出返回码
        ESP_LOGI(TAG, "Power mode switch to sleep completed. ret_code=%d", response->ret_code);

        // 要注意释放内存，否则可能导致内存不足
        free(response);
    } else {
        // 如果切换失败，打印错误日志
        ESP_LOGE(TAG, "Failed to switch power mode to sleep.");
    }
}
```

## 测试

完成上述代码修改后，按照以下步骤进行测试：

1. 重新构建项目

2. 通过 USB 数据线将固件写入开发板

3. 长按 BOOT 按键直至与相机建立蓝牙连接（LED 指示灯会显示连接状态）

4. 连接成功后，单击 BOOT 按键，此时可以观察到相机进入休眠模式，显示屏关闭

如果一切正常，相机将成功进入休眠模式。您可以通过查看串口日志来确认命令的执行状态和返回码。
