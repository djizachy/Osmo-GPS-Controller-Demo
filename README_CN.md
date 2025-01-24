# Osmo Action GPS 蓝牙遥控器 (ESP32-C6-Example)

  ![](https://img.shields.io/badge/version-V1.0.0-red.svg) ![](https://img.shields.io/badge/platform-rtos-blue.svg) ![](https://img.shields.io/badge/license-MIT-purple.svg)

<p align="center">
  <br><a href="README.md">English</a> | 中文
</p>

## 简介

本项目提供了一套运行在 ESP32-C6 开发板上的代码（基于 ESP-IDF 框架），演示了如何解析、处理并发送 DJI R SDK 协议以控制相机。示例程序实现了基本的遥控功能，包括：长按 BOOT 按键连接到最近的（兼容的）Osmo Action 设备、单击控制拍摄、快速切换模式以及基于 LC76G GNSS 模块进行 GPS 数据推送。同时，程序根据设备状态动态调整 RGB LED 灯的显示。

## 主要功能特性

- **协议解析**：`protocol` 协议层展示了如何解析 DJI R SDK 协议。
- **GPS 数据推送**：通过 LC76G GNSS 模块以 10Hz 频率收集 GPS 数据，经过解析后实时推送至相机。
- **按键支持**：支持单击（开始 / 停止录像）和长按（寻找并连接最近的相机）操作。
- **RGB 灯支持**：实时监控系统状态，根据状态变化动态调整 RGB LED 灯的颜色。
- **其他功能展示**：切换相机至指定模式、QS 快速切换模式、相机状态订阅、查询相机版本号等功能。

## 开发环境

**软件**：ESP-IDF v5.5

**硬件**：

- ESP32-C6-WROOM-1
- LC76G GNSS Module
- DJI Osmo Action 5 Pro / DJI Osmo Action 4

硬件连接涉及 ESP32-C6-WROOM-1 与 LC76G GNSS Module 之间的连接，具体连接方式如下：

- **ESP32-C6 GPIO5** 连接到 **LC76G RX**
- **ESP32-C6 GPIO4** 连接到 **LC76G TX**
- **ESP32-C6 5V** 连接到 **LC76G VCC**
- **ESP32-C6 GND** 连接到 **LC76G GND**

请确保正确连接各个引脚，尤其是 TX 和 RX 引脚的互连，以保证数据传输的正常进行。

<img title="Hardware Wiring Diagram" src="docs/images/hardware_wiring_diagram.png" alt="Hardware Wiring Diagram" data-align="inline" width="711">

## 快速开始

- 安装 ESP-IDF 工具链，具体安装步骤可参考下方的参考文档。我们推荐在 VSCode 中安装 ESP-IDF 插件，插件下载地址：[ESP-IDF 插件 - VSCode](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)

- 接下来，检查项目中的 `.vscode/settings.json` 文件，确保 IDF 相关的参数配置正确。

- 完成环境配置后，编译并烧录代码到开发板，使用 monitor 查看实时日志。您可以通过观察开发板上的 RGB 灯状态来了解当前设备状态：红色表示未初始化，黄色表示 BLE 初始化完成，设备已准备就绪。

- 长按 BOOT 按键时，RGB 灯蓝色闪烁，表示正在寻找并连接最近的 Osmo Action 设备。蓝色常亮表示 BLE 已连接，绿色常亮表示协议已连接，能够正常收发命令。紫色常亮表示协议已连接并且有 GPS 信号。

- 单击 BOOT 按键时，相机开始或结束拍摄。长时间录制时，RGB 灯会闪烁。

## 项目结构

```
├── ble              # 蓝牙设备层
├── data             # 数据层
├── logic            # 逻辑层
├── protocol         # 协议层
├── main             # 主程序入口
├── utils            # 工具函数
└── CMakeLists.txt   # 项目构建文件
```

- **ble**：负责 ESP32 与相机之间的 BLE 连接、数据读写等操作。
- **protocol**：负责协议帧的封装和解析，确保数据通信的正确性。
- **data**：负责存储解析后的数据，基于 Entry 提供一套高效的读写逻辑，供逻辑层调用。
- **logic**：实现具体功能，如请求连接、按键操作、GPS 数据处理、相机状态管理、命令发送、灯光控制等。
- **utils**：工具类，用来实现 CRC 校验等。
- **main**：程序入口。

## 协议解析说明

下面这张图展示了程序在解析帧时的大致流程：

<img title="Protocol Parsing Diagram" src="docs/images/protocol_parsing_diagram.png" alt="Protocol Parsing Diagram" data-align="inline" width="711">

详细文档请参阅：[协议解析说明文档](docs/protocol_CN.md)

## 数据层说明

数据层作为帧的发送和接收中转站，定义了一个大小为 10 的 `s_entries` 数组。每个 entry 包括 `seq`、`is_seq_based`、`cmd_set`、`cmd_id` 和 `parse_result` 等字段，并配备了 LRU 和定时删除两种机制，以确保数据层在有限的空间内始终可用。

数据层提供了两种数据读写接口：`data_write_with_response` 和 `data_write_without_response`。

当调用 `data_write_with_response` 时，会分配一个 entry 用于接收解析结果，然后需要调用 `data_wait_for_result_by_seq` 来获取结果。

为什么需要定义 `data_wait_for_result_by_cmd`？有一种情况：在 `connect_logic` 中，当相机连接时，可能会主动发送命令帧给遥控器，此时 `seq` 不是我们定义的，因此需要通过 `CmdSet` 和 `CmdID` 来获取解析结果。

此外，还定义了 `receive_camera_notify_handler` 函数，这是 BLE 层调用的回调函数，用于处理相机发送的命令。

更多细节请参阅 `data.c` 源代码。

## GPS 推送示例

LC76G GNSS 模块的 GNRMC 和 GNGGA 数据最大支持 10Hz 的更新频率，前提是我们需要向该模块发送相应的指令：

```c
// "$PAIR050,1000*12\r\n" 为 1Hz 更新率
// "$PAIR050,500*26\r\n" 为 5Hz 更新率
// "$PAIR050,100*22\r\n" 为 10Hz 更新率
char* gps_command = "$PAIR050,100*22\r\n";  // （>1Hz 仅 RMC 和 GGA 支持）
uart_write_bytes(UART_GPS_PORT, gps_command, strlen(gps_command));
```

接下来启动一个任务，设置其优先级为 0。需要注意与其他任务的优先级配置，因为 GPS 推送频繁执行，可能会抢占其他任务的时间片。获取到的 GPS 原始数据如下：

```
$GNRMC,074700.000,A,2234.732734,N,11356.317512,E,1.67,285.57,150125,,,A,V*03
$GNGGA,074700.000,2234.732734,N,11356.317512,E,1,7,1.31,47.379,M,-2.657,M,,*65
```

在解析大量类似的字符串以获取经纬度、速度分量等信息时，需要剔除无效数据，并可采用一些方法对数据进行矫正和精度提升。由于解析过程频繁执行，任务执行时还需注意看门狗可能超时，因此程序中适当地使用了 `vTaskDelay` 来重置看门狗。本程序仅采用简单的解析方法，进行数据推送演示，请参阅 `gps_logic` 中的 `Parse_NMEA_Buffer` 和 `gps_push_data` 函数。

有 GPS 信号时（RGB 灯紫色常亮），开始录制一段视频，结束录制后可以在 DJI Mimo APP 的仪表盘中查看相应的数据。

## 如何添加功能

**在准备添加功能前，请确保已经完整阅读了协议解析说明文档，并理解数据层的工作机制。**

### 新增命令支持

发送或解析命令帧和应答帧时，只需简单的三步：

* 在 `dji_protocol_data_structures` 中定义帧结构体。

* 在 `dji_protocol_data_descriptors` 中定义三元组，并为其提供对应的 `creator` 和 `parser`。如果没有实现，可以设置为 `NULL`，当解析函数找不到对应的 `creator` 或 `parser` 时，构建或解析帧的过程将停止。

* 在逻辑层（`logic`）中定义相应函数，编写业务逻辑，调用命令逻辑（`command_logic`）中的 `send_command` 函数。

如果在逻辑层新增了 `.c` 文件，请确保修改 `main/CMakeLists.txt` 文件。

关于 `send_command` 函数，你需要知道：除了传入 `CmdSet`、`CmdID` 和帧结构体，还需要传入 `CmdType`，即帧类型，定义在 `enums_logic` 中：

```c
typedef enum {
    CMD_NO_RESPONSE = 0x00,      // 命令帧 - 发送数据后不需要应答                        
    CMD_RESPONSE_OR_NOT = 0x01,  // 命令帧 - 发送数据后需要应答，没收到结果不报错
    CMD_WAIT_RESULT = 0x02,      // 命令帧 - 发送数据后需要应答，没收到结果会报错

    ACK_NO_RESPONSE = 0x20,      // 应答帧 - 不需要应答 (00100000)
    ACK_RESPONSE_OR_NOT = 0x21,  // 应答帧 - 需要应答，没收到结果不报错 (00100001)
    ACK_WAIT_RESULT = 0x22       // 应答帧 - 需要应答，没收到结果会报错 (00100010)
} cmd_type_t;
```

因此，若要支持某个命令帧或应答帧的创建，应在 `creator` 中实现创建功能；若要支持解析，需在 `parser` 中编写解析功能。

除此之外，`send_command` 函数会根据帧类型决定是否阻塞等待数据返回，适用于发送-接收和只发送的场景。如果需要直接接收数据，则应调用 `data_wait_for_result_by_cmd` 函数。

### 修改回调函数

本程序主要在这几个地方使用了回调函数：

- **data** 数据层中的 `receive_camera_notify_handler`：在接收到 BLE 通知后调用，用于接收相机发送的数据。

- **status_logic** 中的 `update_camera_state_handler`：由 `data.c` 的 `receive_camera_notify_handler` 调用，用于更新相机的状态信息。

- **connect_logic** 中的 `receive_camera_disconnect_handler`：在 BLE 断开连接事件后调用，用于处理意外重连和主动断开连接等状态变化。

- **light_logic** 中的 `led_state_timer_callback` 和 `led_blink_timer_callback`：用于根据相应状态变化控制 RGB 灯的显示（定时器优先级默认为 1）。

### 定义按键功能

在 `key_logic` 中，为 BOOT 按键配置了长按和单击事件，并实现了相应的逻辑操作。同时，可以在此处添加更多按键和功能函数。按键扫描任务的优先级被配置为 2，需要注意的是，如果存在其他频繁执行的任务，应合理调整优先级配置，否则可能导致按键响应不灵敏或失效。

### 添加休眠功能示例

阅读完以上文档后，你可以开始尝试新增一个新功能：单击 BOOT 按钮让相机休眠。

具体示例参阅文档：[相机休眠示例文档](docs/camera_sleep_example_CN.md)

## 参考文档

可以参考以下文档，对项目有更全面的了解：

- **ESP-IDF**：[ESP-IDF 官方 GitHub 仓库](https://github.com/espressif/esp-idf/)

- **LC76G GNSS Module**：[LC76G GNSS Module - Waveshare Wiki](https://www.waveshare.net/wiki/LC76G_GPS_Module)

- **ESP32-C6-WROOM-1**：[ESP32-C6-DevKitC-1 v1.2 - ESP32-C6 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)

## 关于 PR

DJI 开发团队始终致力于提升您的开发体验，也欢迎您的贡献，但 PR 代码审查可能会有所延迟。如果您有任何疑问，欢迎通过电子邮件与我们联系。
