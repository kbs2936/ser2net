/*
 * @Author: 刘煌 225225@nd.com
 * @Date: 2022-05-13 13:54:12
 * @LastEditors: 刘煌 225225@nd.com
 * @LastEditTime: 2022-05-13 14:34:11
 * @FilePath: /ser2net/config.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
// config: ////////////////////////////////////////////////////////////
#ifndef _ESP_SERIAL_BRIDGE_CONFIG_H_
#define _ESP_SERIAL_BRIDGE_CONFIG_H_

#define PROTOCOL_TCP
#ifndef MAX_NMEA_CLIENTS
#define MAX_NMEA_CLIENTS 1
#endif

unsigned char qos = 1; //subscribe qos
bool retained = false;

#define ZGB_RST_PIN     12
#define TTY_SEL0_PIN    16
#define TTY_SEL1_PIN    5
/*************************  COM Port 0 *******************************/
#define UART_BAUD0 115200        // Baudrate UART0
#define SERIAL_PARAM0 SERIAL_8N1 // Data/Parity/Stop UART0

#define BUFFERSIZE 1024

#define DEBUG 1

#endif
