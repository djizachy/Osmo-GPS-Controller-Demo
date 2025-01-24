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

#ifndef __GPS_LOGIC_H__
#define __GPS_LOGIC_H__

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

// UART Configuration
// UART 配置
#define UART_GPS_TXD_PIN (GPIO_NUM_5)
#define UART_GPS_RXD_PIN (GPIO_NUM_4)
#define UART_GPS_PORT LP_UART_NUM_0
#define RX_BUF_SIZE 800

typedef struct {
    // Time
    // 时间
    uint8_t Year;             // Year
                              // 年
    uint8_t Month;            // Month
                              // 月
    uint8_t Day;              // Day
                              // 日
    uint8_t Hour;             // Hour
                              // 时
    uint8_t Minute;           // Minute
                              // 分
    double Second;            // Second
                              // 秒

    // Position
    // 位置
    double Latitude;          // Latitude
                              // 纬度
    char Lat_Indicator;       // N/S
    double Longitude;         // Longitude
                              // 经度
    char Lon_Indicator;       // E/W

    // Other Information
    // 其他信息
    double Speed_knots;       // Ground Speed (knots)
                              // 地面速度 (节)
    double Course;            // Course (degrees)
                              // 航向 (度)
    double Altitude;          // Altitude (meters)
                              // 海拔高度 (米)
    uint8_t Num_Satellites;   // Number of Visible Satellites
                              // 可见卫星数量

    // Calculated Velocity Components
    // 计算后的速度分量
    double Velocity_North;    // Northward Velocity (m/s)
                              // 向北速度 (米/秒)
    double Velocity_East;     // Eastward Velocity (m/s)
                              // 向东速度 (米/秒)
    double Velocity_Descend;  // Descent Velocity (m/s)
                              // 下降速度 (米/秒)

    // Status
    // 状态
    uint8_t Status;          // 1: Both RMC and GGA valid, 0: Other cases
                             // 1: RMC和GGA都有效, 0: 其他情况
    uint8_t RMC_Valid;       // Whether RMC data is valid
                             // RMC 数据是否有效
    uint8_t GGA_Valid;       // Whether GGA data is valid
                             // GGA 数据是否有效
    double RMC_Latitude;     // Latitude from RMC
                             // RMC 的纬度
    double RMC_Longitude;    // Longitude from RMC
                             // RMC 的经度
    double GGA_Latitude;     // Latitude from GGA
                             // GGA 的纬度
    double GGA_Longitude;    // Longitude from GGA
                             // GGA 的经度
} GPS_Data_t;

void initSendGpsDataToCameraTask(void);

bool is_gps_found(void);

bool is_current_gps_data_valid(void);

#endif