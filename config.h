#ifndef _ESP_SERIAL_BRIDGE_CONFIG_H_
#define _ESP_SERIAL_BRIDGE_CONFIG_H_


#define MAX_NMEA_CLIENTS 1
#define TCP_PORT         8880


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
