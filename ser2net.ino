#include "config.h"
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager

// For debug log output/update FW
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

void setup()
{
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
  配置 WIFI
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
    LOGD("failed to connect, now reset");
    delay(100);
    ESP.reset();
  }

  LOGD("connect wifi success");
}

void loop()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    LOGD("YES");
  }
  else
  {
    LOGD("NO");
  }

  delay(2000);
}
