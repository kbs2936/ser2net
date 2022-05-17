#include "config.h"
#include <WiFiManager.h>
#include <WiFiClient.h>
#include <Adafruit_NeoPixel.h>

//串口1，打印调试日志
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

//串口0，对接2530串口
HardwareSerial *COM = &Serial;

// wifi server和数据收发缓冲区
WiFiServer *server;
WiFiClient *TCPClient[MAX_NMEA_CLIENTS];
uint8_t buf1[BUFFERSIZE];
uint16_t i1 = 0;
uint8_t buf2[BUFFERSIZE];
uint16_t i2 = 0;

//loop过程中检测到wifi断开连接，则复位此标志位
bool isWiFiConnected = false;

//灯颜色枚举
enum LedColor
{
  LedColorRed = 0,
  LedColorGreen = 1,
  LedColorBlue = 2,
  LedColorBlack = 3,
  LedColorOff = 4
};

//(灯总数,使用引脚,WS2812B一般都是800这个参数不用动)
Adafruit_NeoPixel WS2812B(1, LED_PIN, NEO_GRB + NEO_KHZ800);

//配置灯的颜色函数
void ledShowColor(LedColor color)
{
  switch (color)
  {
  case LedColorRed:
    singleLedColor(0, 255, 0, 0);
    break;

  case LedColorGreen:
    singleLedColor(0, 0, 255, 0);
    break;

  case LedColorBlue:
    singleLedColor(0, 0, 0, 255);
    break;

  case LedColorBlack:
    singleLedColor(0, 0, 0, 0);
    break;

  case LedColorOff:
    WS2812B.clear();
    WS2812B.show();
    break;

  default:

    break;
  }
}

/********************
   设置灯带中某一个灯的颜色。
   单灯颜色(灯位置,红,绿,蓝)
   下标从0开始
********************/
void singleLedColor(int index, int R, int G, int B)
{
  WS2812B.setPixelColor(index, (((R & 0xffffff) << 16) | ((G & 0xffffff) << 8) | B));
  WS2812B.show();
}

// setup
void setup()
{
  //配置LED灯
  WS2812B.begin();
  WS2812B.clear();
  WS2812B.setBrightness(100);
  ledShowColor(LedColorRed);

  /*
  拨盘开关引脚配置
  opt3：USB TTY <==> ESP 8266 debug port UART1.  ESP 8266 TTY UART0 <==> E18 TTY
  */
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
  isWiFiConnected = true;
  ledShowColor(LedColorOff);
  LOGD("TCP server: %s:%d", WiFi.localIP().toString().c_str(), TCP_PORT);
  static WiFiServer server_0(TCP_PORT);
  server = &server_0;
  server->begin();
  server->setNoDelay(true);
}

// loop
void loop()
{
  //断网亮灯，重新联网灭灯
  if (WiFi.status() != WL_CONNECTED)
  {
    //通过标志位，loop期间断网不要重复设置灯红色，只要设置一次即可，网络恢复同理也不要灭灯多次
    if (isWiFiConnected)
    {
      isWiFiConnected = false;
      ledShowColor(LedColorRed);
      LOGD("Wifi disconnect");
    }
  }
  else
  {
    if (!isWiFiConnected)
    {
      ledShowColor(LedColorOff);
      LOGD("Wifi connect");
    }
    isWiFiConnected = true;
  }

  //把所有断开的客户端移除
  for (byte i = 0; i < MAX_NMEA_CLIENTS; i++)
  {
    if (TCPClient[i] && !TCPClient[i]->connected())
    {
      TCPClient[i]->stop();
      delete TCPClient[i];
      TCPClient[i] = NULL;
      LOGD("Client disconnected 1");
    }
  }

  //如果有新客户端进来，读取过 server->available() 后，这个 hasClient 就不会为true了，直到有新的客户端连进来
  if (server->hasClient())
  {
    //找出被上面那段代码移除的客户端的数组位置、或者当前检已经断开的客户端并移除，然后读取 available 把新的客户端加到数组
    for (byte i = 0; i < MAX_NMEA_CLIENTS; i++)
    {
      if (!TCPClient[i] || !TCPClient[i]->connected())
      {
        if (TCPClient[i])
        {
          TCPClient[i]->stop();
          delete TCPClient[i];
          TCPClient[i] = NULL;
          LOGD("Client disconnected 2");
        }
        TCPClient[i] = new WiFiClient; //先放指针，再用指针指向进来的客户端
        *TCPClient[i] = server->available();
        LOGD("New client for COM");
      }
    }
    //遍历之后，如果之前数组里的客户端已满，并且都是连接的状态，无法接受新的客户端，则把新客户端读走并 stop
    WiFiClient TmpserverClient = server->available();
    if (TmpserverClient)
    {
      TmpserverClient.stop();
      LOGD("Client's array is full, stop new coming");
    }
  }

  if (COM != NULL)
  {
    //读取 tcp client 发来的数据，然后通过 串口0 tx 发给2530
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

    //读取2530发来的数据，发给 tcp client
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
