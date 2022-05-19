#ifndef _ESP_SERIAL_BRIDGE_CONFIG_H_
#define _ESP_SERIAL_BRIDGE_CONFIG_H_

//最大tcp client连接数和默认TCP服务器端口
#define MAX_NMEA_CLIENTS 1
#define TCP_PORT 8880

//拨盘开关和zigbee复位引脚
#define TTY_SEL0_PIN 16
#define TTY_SEL1_PIN 5
#define ZGB_RST_PIN 12

// ws2812b灯的引脚和个数
#define LED_PIN 15
#define NUM_LEDS 1

//通讯串口0波特率和模式
#define UART_BAUD0 115200
#define SERIAL_PARAM0 SERIAL_8N1

#define BUFFERSIZE 1024

#define DEBUG 1

//灯颜色枚举
enum LedColor
{
    LedColorRed = 0,
    LedColorGreen = 1,
    LedColorBlue = 2,
    LedColorBlack = 3,
    LedColorOff = 4
};

#endif
