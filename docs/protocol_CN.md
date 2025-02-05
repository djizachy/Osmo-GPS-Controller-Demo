# 协议解析说明文档

## 什么是 DJI R SDK 协议

该协议用来与 DJI Osmo Action 5 Pro / DJI Osmo Action 4 进行通信，帧结构如下所示：

```
|  SOF   | Ver/Length | CmdType |  ENC   |  RES   |  SEQ   | CRC-16 |  DATA  | CRC-32 |
|------- |------------|---------|--------|--------|--------|--------|--------|--------|
| 1-byte |   2-byte   | 1-byte  | 1-byte | 3-byte | 2-byte | 2-byte | n-byte | 4-byte |
```

其中，CRC-16 的值是对 SOF 到 SEQ 段进行 CRC16 校验后的结果，CRC-32 的值是对 SOF 到 DATA 段进行 CRC32 校验后的结果。

## DATA 数据段

本程序的核心在于封装和解析 DATA 数据段。虽然 DATA 数据段的长度不固定，但开头的两个字节始终为 CmdSet 和 CmdID，结构如下所示：

```
|  CmdSet  |  CmdID   |  Data Payload  |
|----------|----------|----------------|
|  1-byte  |  1-byte  |     n-byte     |
```

## 协议层

协议层是本程序为方便解析协议帧，单独抽离出来的一个模块。文件结构如下：

```
protocol/
├── dji_protocol_data_descriptors.c
├── dji_protocol_data_descriptors.h
├── dji_protocol_data_processor.c
├── dji_protocol_data_processor.h
├── dji_protocol_data_structures.c
├── dji_protocol_data_structures.h
├── dji_protocol_parser.c
└── dji_protocol_parser.h
```

- **dji_protocol_parser**：负责 DJI R SDK 协议帧的封装与解析。
- **dji_protocol_data_processor**：负责 DATA 段的封装与解析。
- **dji_protocol_data_descriptors**：为每个功能定义三元组 (CmdSet, CmdID) - creator - parser，以便进行功能扩展。
- **dji_protocol_data_structures**：为命令帧和应答帧定义结构体。

<img title="Protocol Layer" src="images/protocol_layer.png" alt="Protocol Layer" data-align="center" width="652">

上述图片展示了协议层如何解析 DJI R SDK 帧，组装过程也是类似的。

在 DJI R SDK 协议不变的情况下，您无需修改 `dji_protocol_parser`。同样，`dji_protocol_data_processor` 也无需修改，因为它调用的是 `dji_protocol_data_descriptors` 中定义的通用 `creator` 和 `parser` 方法：

```c
typedef uint8_t* (*data_creator_func_t)(const void *structure, size_t *data_length, uint8_t cmd_type);
typedef int (*data_parser_func_t)(const uint8_t *data, size_t data_length, void *structure_out, uint8_t cmd_type);
```

因此，新增功能的解析时，只需在 `dji_protocol_data_structures` 中定义帧结构体，在 `dji_protocol_data_descriptors` 中新增对应的 `creator` 和 `parser` 函数，并将其加入到 `data_descriptors` 三元组中即可。

以下是本程序已支持的命令功能：

```c
/* 结构体支持，但要为每个结构体定义 creator 和 parser */
const data_descriptor_t data_descriptors[] = {
    // 拍摄模式切换
    {0x1D, 0x04, (data_creator_func_t)camera_mode_switch_creator, (data_parser_func_t)camera_mode_switch_parser},
    // 版本号查询
    {0x00, 0x00, NULL, (data_parser_func_t)version_query_parser},
    // 拍录控制
    {0x1D, 0x03, (data_creator_func_t)record_control_creator, (data_parser_func_t)record_control_parser},
    // GPS 数据推送
    {0x00, 0x17, (data_creator_func_t)gps_data_creator, (data_parser_func_t)gps_data_parser},
    // 连接请求
    {0x00, 0x19, (data_creator_func_t)connection_data_creator, (data_parser_func_t)connection_data_parser},
    // 相机状态订阅
    {0x1D, 0x05, (data_creator_func_t)camera_status_subscription_creator, NULL},
    // 相机状态推送
    {0x1D, 0x02, NULL, (data_parser_func_t)camera_status_push_data_parser},
    // 按键上报
    {0x00, 0x11, (data_creator_func_t)key_report_creator, (data_parser_func_t)key_report_parser},
};
```

详细的协议文档可联系 DJI 人员获取。
