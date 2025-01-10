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
	double Lon;     // GPS Latitude and longitude
	double Lat;
    UBYTE Lon_area;
    UBYTE Lat_area;
    UBYTE Time_H;   // Time
    UBYTE Time_M;
    UBYTE Time_S;
    UBYTE Status;   // 1:Successful positioning 0:Positioning failed
}GNRMC;

void initSendGpsDataToCameraTask(void);

#endif
