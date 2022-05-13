#ifndef _ESP_SERIAL_BRIDGE_CONFIG_H_
#define _ESP_SERIAL_BRIDGE_CONFIG_H_

//最大tcp client连接数
#define MAX_NMEA_CLIENTS 1
#define TCP_PORT         8880

//拨盘开关连接8266芯片的引脚号定义
#define TTY_SEL0_PIN    16
#define TTY_SEL1_PIN    5

/*************************  COM Port 0 *******************************/
#define UART_BAUD0 115200        // Baudrate UART0
#define SERIAL_PARAM0 SERIAL_8N1 // Data/Parity/Stop UART0

#define BUFFERSIZE 1024

#define DEBUG 1

#endif
