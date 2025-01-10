#include <string.h>
#include <stdio.h>

#include "gps_logic.h"
#include "connect_logic.h"

static char buff_t[RX_BUF_SIZE]={0};
static GNRMC GPS;

// GPS 数据解析函数
static GNRMC GNRMC_L76X_Gat_GNRMC(void)
{
    UWORD add = 0, x = 0, z = 0, i = 0;
    UDOUBLE Time = 0, latitude = 0, longitude = 0;

    GPS.Status = 0;
    GPS.Time_H = 0;
    GPS.Time_M = 0;
    GPS.Time_S = 0;
    
    add = 0;
    while (add < RX_BUF_SIZE) {
        if (buff_t[add] == '$' && buff_t[add + 1] == 'G' && (buff_t[add + 2] == 'N' || buff_t[add + 2] == 'P') &&
            buff_t[add + 3] == 'R' && buff_t[add + 4] == 'M' && buff_t[add + 5] == 'C') {
            x = 0;
            for (z = 0; x < 12; z++) {
                if (buff_t[add + z] == '\0') {
                    break;
                }
                if (buff_t[add + z] == ',') {
                    x++;
                    if (x == 1) { // The first comma is followed by time
                        Time = 0;
                        for (i = 0; buff_t[add + z + i + 1] != '.'; i++) {
                            if (buff_t[add + z + i + 1] == '\0') {
                                break;
                            }
                            if (buff_t[add + z + i + 1] == ',')
                                break;
                            Time = (buff_t[add + z + i + 1] - '0') + Time * 10;
                        }

                        GPS.Time_H = Time / 10000 + 8;
                        GPS.Time_M = Time / 100 % 100;
                        GPS.Time_S = Time % 100;
                        if (GPS.Time_H >= 24)
                            GPS.Time_H = GPS.Time_H - 24;
                    } else if (x == 2) {
                        // A indicates that it has been positioned
                        // V indicates that there is no positioning.
                        if (buff_t[add + z + 1] == 'A') {
                            GPS.Status = 1;
                        } else {
                            GPS.Status = 0;
                        }
                    } else if (x == 3) {
                        latitude = 0;
                        // If you need to modify, please re-edit the calculation method below.
                        for (i = 0; buff_t[add + z + i + 1] != ','; i++) {
                            if (buff_t[add + z + i + 1] == '\0') {
                                break;
                            }
                            if (buff_t[add + z + i + 1] == '.') {
                                continue;
                            }
                            latitude = (buff_t[add + z + i + 1] - '0') + latitude * 10;
                        }
                        GPS.Lat = latitude / 1000000.0;
                    } else if (x == 4) {
                        GPS.Lat_area = buff_t[add + z + 1];
                    } else if (x == 5) {
                        longitude = 0;
                        for (i = 0; buff_t[add + z + i + 1] != ','; i++) {
                            if (buff_t[add + z + i + 1] == '\0') {
                                break;
                            }
                            if (buff_t[add + z + i + 1] == '.')
                                continue;
                            longitude = (buff_t[add + z + i + 1] - '0') + longitude * 10;
                        }
                        GPS.Lon = longitude / 1000000.0;
                    } else if (x == 6) {
                        GPS.Lon_area = buff_t[add + z + 1];
                    }
                }
            }
            add = 0;
            break;
        }
        if (buff_t[add + 5] == '\0') {
            add = 0;
            break;
        }
        add++;
        if (add > RX_BUF_SIZE) {
            add = 0;
            break;
        }
    }
    return GPS;
}

// 初始化 GPS UART
static void initUartGps(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,               //LP UART
        .source_clk = LP_UART_SCLK_DEFAULT,     //LP UART
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_GPS_PORT, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_GPS_PORT, &uart_config);
    uart_set_pin(UART_GPS_PORT, UART_GPS_TXD_PIN, UART_GPS_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void rx_task_GPS(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK_GPS";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);

    while (1) {
        const int rxBytes = uart_read_bytes(UART_GPS_PORT, data, RX_BUF_SIZE, 20 / portTICK_PERIOD_MS);
        
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);

            // 将读取到的数据存储到全局缓冲区 buff_t 中
            memcpy(buff_t, data, rxBytes);

            // 解析数据
            GNRMC gps = GNRMC_L76X_Gat_GNRMC();

            // TODO：GPS 数据发送给相机
            if(connect_logic_get_state() == CONNECT_STATE_PROTOCOL_CONNECTED && gps.Status == 1){
                ESP_LOGI("GPS Data", "Parsed GPS Data: Time=%02d:%02d:%02d, Lat=%f, Lon=%f, Status=%d", gps.Time_H, gps.Time_M, gps.Time_S, gps.Lat, gps.Lon, gps.Status);
            }
        }

        // 如果没有数据读取，休眠一小段时间，避免任务占用 CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}

void initSendGpsDataToCameraTask(void)
{
    initUartGps();
    xTaskCreate(rx_task_GPS, "uart_rx_task_GPS", 1024 * 2, NULL, configMAX_PRIORITIES - 1, NULL);
    ESP_LOGI("MAIN", "uart_rx_task_GPS are running\n");
}
