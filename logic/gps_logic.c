#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "gps_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC_GPS"

static char buff_t[RX_BUF_SIZE]={0};

static GPS_Data_t GPS_Data = {
    .Year = 0,
    .Month = 0,
    .Day = 0,
    .Hour = 0,
    .Minute = 0,
    .Second = 0.0,

    .Latitude = 0.0,
    .Lat_Indicator = 'N',
    .Longitude = 0.0,
    .Lon_Indicator = 'E',

    .Status = 0,
    .Speed_knots = 0.0,
    .Course = 0.0,
    .Altitude = 0.0,
    .Num_Satellites = 0,

    .Velocity_North = 0.0,
    .Velocity_East = 0.0,
    .Velocity_Descend = 0.0
};

bool is_gps_found(void) {
    if (GPS_Data.Status == 1) {
        return true;
    }
    return false;
}

static double Previous_Altitude = 0.0;
static double Previous_Time = 0.0;

// 将NMEA格式的经纬度转换为十进制度
double Convert_NMEA_To_Degree(const char *nmea, char direction) {
    double deg = 0.0;
    double min = 0.0;

    // 确定整数部分和小数部分
    const char *dot = nmea;
    while (*dot && *dot != '.') { // 找到小数点的位置
        dot++;
    }

    // 计算整数部分
    int int_part = 0;
    const char *ptr = nmea;
    while (ptr < dot && isdigit((unsigned char)*ptr)) {
        int_part = int_part * 10 + (*ptr - '0'); // 累积数字
        ptr++;
    }

    // 计算小数部分
    double frac_part = 0.0;
    double divisor = 10.0;
    ptr = dot + 1; // 跳过小数点
    while (*ptr && isdigit((unsigned char)*ptr)) {
        frac_part += (*ptr - '0') / divisor;
        divisor *= 10.0;
        ptr++;
    }

    // 合并整数部分和小数部分
    deg = int_part + frac_part;

    // 分离度和分
    min = deg - ((int)(deg / 100)) * 100;
    deg = ((int)(deg / 100)) + min / 60.0;

    // 根据方向调整正负
    if (direction == 'S' || direction == 'W') {
        deg = -deg;
    }

    return deg;
}

// 解析GNRMC语句：$GNRMC,074700.000,A,2234.732734,N,11356.317512,E,1.67,285.57,150125,,,A,V*03
void Parse_GNRMC(char *sentence) {
    char *token = strtok(sentence, ",");
    int field = 0;

    double temp_latitude = 0.0;
    double temp_longitude = 0.0;

    while (token != NULL) {
        field++;
        switch (field) {
            case 2: {
                // 时间 hhmmss.sss
                uint8_t hour = 0, minute = 0;
                double second = 0.0;

                // 手动解析时间
                const char *ptr = token;
                hour = (ptr[0] - '0') * 10 + (ptr[1] - '0');
                minute = (ptr[2] - '0') * 10 + (ptr[3] - '0');
                second = atof(ptr + 4);

                GPS_Data.Hour = hour;
                GPS_Data.Minute = minute;
                GPS_Data.Second = second;
                break;
            }
            case 3:
                // 状态 A/V
                GPS_Data.Status = (token[0] == 'A') ? 1 : 0;
                break;
            case 4:
                // 纬度（暂时存储）
                temp_latitude = Convert_NMEA_To_Degree(token, 'N'); // 默认方向为北
                break;
            case 5:
                // 更新纬度方向
                GPS_Data.Lat_Indicator = token[0];
                GPS_Data.Latitude = (GPS_Data.Lat_Indicator == 'S') ? -temp_latitude : temp_latitude;
                break;
            case 6:
                // 经度（暂时存储）
                temp_longitude = Convert_NMEA_To_Degree(token, 'E'); // 默认方向为东
                break;
            case 7:
                // 更新经度方向
                GPS_Data.Lon_Indicator = token[0];
                GPS_Data.Longitude = (GPS_Data.Lon_Indicator == 'W') ? -temp_longitude : temp_longitude;
                break;
            case 8:
                // 地面速度 (节)
                GPS_Data.Speed_knots = atof(token);
                break;
            case 9:
                // 航向 (度)
                GPS_Data.Course = atof(token);
                break;
            case 10: {
                // 日期 ddmmyy
                uint8_t day = 0, month = 0, year = 0;

                // 手动解析日期
                const char *ptr = token;
                day = (ptr[0] - '0') * 10 + (ptr[1] - '0');
                month = (ptr[2] - '0') * 10 + (ptr[3] - '0');
                year = (ptr[4] - '0') * 10 + (ptr[5] - '0');

                GPS_Data.Day = day;
                GPS_Data.Month = month;
                GPS_Data.Year = year;
                break;
            }
            default:
                break;
        }
        token = strtok(NULL, ",");
    }

    // 计算向北和向东的速度分量 (米/秒)，节转米/秒
    double speed_m_s = GPS_Data.Speed_knots * 0.514444;
    GPS_Data.Velocity_North = speed_m_s * cos(GPS_Data.Course * M_PI / 180.0);
    GPS_Data.Velocity_East = speed_m_s * sin(GPS_Data.Course * M_PI / 180.0);
}

// 解析GNGGA语句，$GNGGA,074700.000,2234.732734,N,11356.317512,E,1,7,1.31,47.379,M,-2.657,M,,*65
void Parse_GNGGA(char *sentence) {
    char *token = strtok(sentence, ",");
    int field = 0;

    while (token != NULL) {
        field++;
        switch (field) {
            case 1:
                // $GNGGA
                break;
            case 2:
                // 时间 hhmmss.sss，可与GNRMC中的时间对比，确保同步
                break;
            case 3:
                // 纬度
                GPS_Data.Latitude = Convert_NMEA_To_Degree(token, GPS_Data.Lat_Indicator);
                break;
            case 4:
                // N/S
                GPS_Data.Lat_Indicator = token[0];
                GPS_Data.Latitude = Convert_NMEA_To_Degree(token - strlen(token), token[0]); // 重新计算
                break;
            case 5:
                // 经度
                GPS_Data.Longitude = Convert_NMEA_To_Degree(token, GPS_Data.Lon_Indicator);
                break;
            case 6:
                // E/W
                GPS_Data.Lon_Indicator = token[0];
                GPS_Data.Longitude = Convert_NMEA_To_Degree(token - strlen(token), token[0]); // 重新计算
                break;
            case 7:
                // 定位质量，0 = 无效，1 = GPS，2 = 差分GPS，...
                break;
            case 8:
                // 可见卫星数量
                GPS_Data.Num_Satellites = (uint8_t) atoi(token);
                break;
            case 9:
                // HDOP，可根据需要解析
                break;
            case 10:
                // 海拔高度 (米)
                GPS_Data.Altitude = atof(token);
                // 计算下降速度 (需要上一高度和时间)
                if (Previous_Time > 0.0) {
                    double delta_time = (GPS_Data.Hour * 3600 + GPS_Data.Minute * 60 + GPS_Data.Second) - Previous_Time;
                    if (delta_time > 0) {
                        GPS_Data.Velocity_Descend = (GPS_Data.Altitude - Previous_Altitude) / delta_time;
                    }
                }
                Previous_Altitude = GPS_Data.Altitude;
                Previous_Time = GPS_Data.Hour * 3600 + GPS_Data.Minute * 60 + GPS_Data.Second;
                break;
            // 其他字段可根据需要解析
            default:
                break;
        }
        token = strtok(NULL, ",");
    }
}

// 主解析函数，处理缓冲区中的所有NMEA语句
void Parse_NMEA_Buffer(char *buffer) {
    char *start = buffer; // 指向字符串的开始
    char *end = NULL; // 指向每行的结束位置

    while ((end = strchr(start, '\n')) != NULL) {
        size_t line_length = end - start; // 计算每行的长度

        if (line_length > 0) {
            char line[RX_BUF_SIZE] = {0}; // 创建一个临时缓冲区存储单行
            strncpy(line, start, line_length); // 将该行拷贝到缓冲区
            line[line_length] = '\0'; // 确保以空字符结尾

            // 解析该行
            if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0) {
                Parse_GNRMC(line);
            } else if (strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GPGGA", 6) == 0) {
                Parse_GNGGA(line);
            }
        }

        start = end + 1; // 移动到下一行的开始
    }

    // 处理最后一行（如果没有以换行符结尾）
    if (*start != '\0') {
        if (strncmp(start, "$GNRMC", 6) == 0 || strncmp(start, "$GPRMC", 6) == 0) {
            Parse_GNRMC(start);
        } else if (strncmp(start, "$GNGGA", 6) == 0 || strncmp(start, "$GPGGA", 6) == 0) {
            Parse_GNGGA(start);
        }
    }
}

void print_gps_data() {
    ESP_LOGI(TAG, 
        "GPS Data: Time=%02d:%02d:%06.3f, Date=%02d-%02d-20%02d, "
        "Lat=%f %c, Lon=%f %c, Speed=%.2f knots, Course=%.2f deg, "
        "Altitude=%.2f m, Satellites=%d, V_North=%.2f m/s, V_East=%.2f m/s, V_Descend=%.2f m/s",
        GPS_Data.Hour, GPS_Data.Minute, GPS_Data.Second,
        GPS_Data.Day, GPS_Data.Month, GPS_Data.Year,
        GPS_Data.Latitude, GPS_Data.Lat_Indicator,
        GPS_Data.Longitude, GPS_Data.Lon_Indicator,
        GPS_Data.Speed_knots, GPS_Data.Course,
        GPS_Data.Altitude, GPS_Data.Num_Satellites,
        GPS_Data.Velocity_North, GPS_Data.Velocity_East,
        GPS_Data.Velocity_Descend
    );
}

void gps_push_data() {
    // 时间转换
    int32_t year_month_day = GPS_Data.Year * 10000 + GPS_Data.Month * 100 + GPS_Data.Day;
    int32_t hour_minute_second = (GPS_Data.Hour + 8) * 10000 + GPS_Data.Minute * 100 + (int32_t)GPS_Data.Second;

    // 经纬度转换
    int32_t gps_longitude = (int32_t)(GPS_Data.Longitude * 1e7);
    int32_t gps_latitude = (int32_t)(GPS_Data.Latitude * 1e7);

    // 高度转换
    int32_t height = (int32_t)(GPS_Data.Altitude * 1000);    // 单位 mm

    // 速度转换
    float speed_to_north = GPS_Data.Velocity_North * 100;    // m/s 转换为 cm/s
    float speed_to_east = GPS_Data.Velocity_East * 100;      // m/s 转换为 cm/s
    float speed_to_wnward = GPS_Data.Velocity_Descend * 100; // m/s 转换为 cm/s

    // 卫星数量
    uint32_t satellite_number = GPS_Data.Num_Satellites;

    // 打印数据
    ESP_LOGI(TAG, "GPS Data:");
    ESP_LOGI(TAG, "  YearMonthDay (uint32_t): %lu", (unsigned long)year_month_day);
    ESP_LOGI(TAG, "  HourMinuteSecond (uint32_t, UTC+8): %lu", (unsigned long)hour_minute_second);
    ESP_LOGI(TAG, "  Longitude (uint32_t, scaled): %lu", (unsigned long)gps_longitude);
    ESP_LOGI(TAG, "  Latitude (uint32_t, scaled): %lu", (unsigned long)gps_latitude);
    ESP_LOGI(TAG, "  Height (uint32_t, mm): %lu", (unsigned long)height);
    ESP_LOGI(TAG, "  Speed to North (float, cm/s): %.2f", speed_to_north);
    ESP_LOGI(TAG, "  Speed to East (float, cm/s): %.2f", speed_to_east);
    ESP_LOGI(TAG, "  Speed to Downward (float, cm/s): %.2f", speed_to_wnward);
    ESP_LOGI(TAG, "  Satellite Number (uint32_t): %lu", (unsigned long)satellite_number);

    // 创建 GPS 数据帧
    gps_data_push_command_frame gps_frame = {
        .year_month_day = year_month_day,
        .hour_minute_second = hour_minute_second,
        .gps_longitude = gps_longitude,
        .gps_latitude = gps_latitude,
        .height = height,
        .speed_to_north = speed_to_north,
        .speed_to_east = speed_to_east,
        .speed_to_wnward = speed_to_wnward,
        .vertical_accuracy = 0,    // 默认精度为 0
        .horizontal_accuracy = 0, // 默认精度为 0
        .speed_accuracy = 0,      // 默认精度为 0
        .satellite_number = satellite_number
    };

    // 推送 GPS 数据到相机，无应答，默认返回 NULL
    gps_data_push_response_frame *response = command_logic_push_gps_data(&gps_frame);
    if (response != NULL) {
        free(response);
    }
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
            data[rxBytes] = '\0'; // 确保字符串结束
            // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);

            // 将读取到的数据存储到全局缓冲区 buff_t 中
            memcpy(buff_t, data, rxBytes);
            buff_t[rxBytes] = '\0'; // 确保缓冲区结束

            // 解析数据
            Parse_NMEA_Buffer(buff_t);

            // 打印解析后的GPS数据
            // print_gps_data();

            if(connect_logic_get_state() == PROTOCOL_CONNECTED && is_gps_found()){
                gps_push_data();
            }
        }

        // 如果没有数据读取，休眠一小段时间，避免任务占用 CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}

void initSendGpsDataToCameraTask(void) {
    initUartGps();
    xTaskCreate(rx_task_GPS, "uart_rx_task_GPS", 1024 * 2, NULL, configMAX_PRIORITIES - 1, NULL);
    ESP_LOGI("MAIN", "uart_rx_task_GPS are running\n");
}
