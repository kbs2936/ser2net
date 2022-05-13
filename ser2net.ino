#include "config.h"
#include <WiFiManager.h>
#include <WiFiClient.h>

// For debug log output
#if (DEBUG)
HardwareSerial *DBGCOM = &Serial1;
#define LOGD(...)                \
  do                             \
  {                              \
    DBGCOM->printf(__VA_ARGS__); \
    DBGCOM->println();           \
  } while (0)
#else
#define LOGD(...)
#endif

HardwareSerial *COM = &Serial;

WiFiServer *server;
WiFiClient *TCPClient[MAX_NMEA_CLIENTS];
uint8_t buf1[BUFFERSIZE];
uint16_t i1 = 0;
uint8_t buf2[BUFFERSIZE];
uint16_t i2 = 0;

static inline void reset_zigbee()
{
  digitalWrite(ZGB_RST_PIN, LOW);
  delay(500);
  digitalWrite(ZGB_RST_PIN, HIGH);
}

void setup()
{
  // opt3：USB TTY <==> ESP 8266 debug port UART1.  ESP 8266 TTY UART0 <==> E18 TTY
  pinMode(TTY_SEL0_PIN, OUTPUT);
  digitalWrite(TTY_SEL0_PIN, LOW);
  pinMode(TTY_SEL1_PIN, OUTPUT);
  digitalWrite(TTY_SEL1_PIN, HIGH);
  delay(500);

  COM->begin(UART_BAUD0, SERIAL_PARAM0);

#if (DEBUG)
  DBGCOM->begin(115200);
#endif

  /*
  配置 WIFI，在 loop 中断网，网络恢复后会自动重连上，不需要用 if (WiFi.status() == WL_CONNECTED) 去判断网络和做重连的操作
  上次如果有配置过wifi账号密码，启动后autoConnect如果连接不成功会等待20s，10s内把路由那边的wifi密码开起来还能连上不会自动变AP，
  不设这个超时连接失败是立刻变AP
  */
  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(10);

  randomSeed(analogRead(15));
  String apName = String("ZigBeeAP_") + String(random(100, 999));
  LOGD("\n\n Begin connect wifi, apName = %s", apName.c_str());

  //网络设置界面exit会走返回这个false。web界面wifi密码连接错误此方法不会返回，还是重新变回ap
  if (!wifiManager.autoConnect(apName.c_str()))
  {
    LOGD("Failed to connect, now reset");
    delay(100);
    ESP.reset();
  }

  // start TCP server
  LOGD("TCP server: %s:%d", WiFi.localIP().toString().c_str(), TCP_PORT);
  static WiFiServer server_0(TCP_PORT);
  server = &server_0;
  server->begin();
  server->setNoDelay(true);

  pinMode(ZGB_RST_PIN, OUTPUT);
  digitalWrite(ZGB_RST_PIN, HIGH);
  reset_zigbee();
}

void loop()
{
  for (byte i = 0; i < MAX_NMEA_CLIENTS; i++)
  {
    //find disconnected spot
    if (TCPClient[i] && !TCPClient[i]->connected())
    {
      TCPClient[i]->stop();
      delete TCPClient[i];
      TCPClient[i] = NULL;
      LOGD("Client disconnected");
    }
  }

  if (server->hasClient())
  {
    for (byte i = 0; i < MAX_NMEA_CLIENTS; i++)
    {
      // find free/disconnected spot
      if (!TCPClient[i] || !TCPClient[i]->connected())
      {
        if (TCPClient[i])
        {
          TCPClient[i]->stop();
          delete TCPClient[i];
          TCPClient[i] = NULL;
          LOGD("Client disconnected");
        }
        TCPClient[i] = new WiFiClient;
        *TCPClient[i] = server->available();
        LOGD("New client for COM");
      }
    }
    // no free/disconnected spot so reject
    // WiFiClient TmpserverClient = server->available();
    // TmpserverClient.stop();
  }

  if (COM != NULL)
  {
    for (byte cln = 0; cln < MAX_NMEA_CLIENTS; cln++)
    {
      if (TCPClient[cln])
      {
        while (TCPClient[cln]->available())
        {
          buf1[i1] = TCPClient[cln]->read(); // read char from TCP port:8880
          if (i1 < BUFFERSIZE - 1)
            i1++;
        }
        if (i1 > 0)
        {
          COM->write(buf1, i1); // now send to UART
#if (DEBUG)
          DBGCOM->printf("TX(%d):\t", i1);
          for (int i = 0; i < i1; i++)
            DBGCOM->printf("%02x ", buf1[i]);
          DBGCOM->println();
#endif
          i1 = 0;
        }
      }
    }

    if (COM->available())
    {
      while (COM->available())
      {
        buf2[i2] = COM->read(); // read char from UART
        if (i2 < BUFFERSIZE - 1)
          i2++;
      }

      if (i2 > 0)
      {
        // now send to WiFi:
        for (byte cln = 0; cln < MAX_NMEA_CLIENTS; cln++)
        {
          if (TCPClient[cln])
            TCPClient[cln]->write(buf2, i2); // send the buffer to TCP port:8880
        }
#if (DEBUG)
        DBGCOM->printf("RX(%d):\t", i2);
        for (int i = 0; i < i2; i++)
          DBGCOM->printf("%02x ", buf2[i]);
        DBGCOM->println();
#endif
        i2 = 0;
      }
    }
  }
}
