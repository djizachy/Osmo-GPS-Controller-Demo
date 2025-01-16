#ifndef __GPS_LOGIC_H__
#define __GPS_LOGIC_H__

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

// UART 配置
#define UART_GPS_TXD_PIN (GPIO_NUM_5)
#define UART_GPS_RXD_PIN (GPIO_NUM_4)
#define UART_GPS_PORT LP_UART_NUM_0
#define RX_BUF_SIZE 800

typedef struct {
    // 时间
    uint8_t Year;             // 年
    uint8_t Month;            // 月
    uint8_t Day;              // 日
    uint8_t Hour;             // 时
    uint8_t Minute;           // 分
    double Second;            // 秒

    // 位置
    double Latitude;          // 纬度
    char Lat_Indicator;       // N/S
    double Longitude;         // 经度
    char Lon_Indicator;       // E/W

    // 状态
    uint8_t Status;           // 1: 定位成功, 0: 定位失败

    // 其他信息
    double Speed_knots;       // 地面速度 (节)
    double Course;            // 航向 (度)
    double Altitude;          // 海拔高度 (米)
    uint8_t Num_Satellites;   // 可见卫星数量

    // 计算后的速度分量
    double Velocity_North;    // 向北速度 (米/秒)
    double Velocity_East;     // 向东速度 (米/秒)
    double Velocity_Descend;  // 下降速度 (米/秒)
} GPS_Data_t;

void initSendGpsDataToCameraTask(void);

bool is_gps_found(void);

#endif
