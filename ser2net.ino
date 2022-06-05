#include "config.h"
#include <WiFiManager.h>
#include <WiFiClient.h>
#include <Adafruit_NeoPixel.h>
#include <Ticker.h>
#include <FS.h>
#include <ArduinoJson.h>

//---------------------------------------------------------------------------------------------------------------------
// Debug串口1，打印调试日志
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

//通讯串口0，对接2530串口
HardwareSerial *COM = &Serial;

// wifi server和数据收发缓冲区
WiFiServer *server;
WiFiClient *TCPClient[MAX_NMEA_CLIENTS];
uint8_t buf1[BUFFERSIZE];
uint16_t i1 = 0;
uint8_t buf2[BUFFERSIZE];
uint16_t i2 = 0;

// loop过程中检测到wifi断开连接，则复位此标志位
bool isWiFiConnected = false;

//(灯总数,使用引脚,WS2812B一般都是800这个参数不用动)
Adafruit_NeoPixel WS2812B(1, LED_PIN, NEO_GRB + NEO_KHZ800);

//断网定时器，setup和loop中多上时间没连上网络则重启
Ticker timer;

// mqtt参数结构体定义及其变量定义
struct MqttConfig
{
  char mqttServer[32];
  char mqttPort[6];
  char mqttUser[32];
  char mqttPass[32];
  char mqttSubTopic[32];
};

struct MqttConfig config = {
    "",
    "1883",
    "",
    "",
    "z2m"};

//用于保存数据的标志
bool isSaveConfig = false;

//---------------------------------------------------------------------------------------------------------------------
/**
 * @description: 从文件系统中读取mqtt的配置参数
 */
void getMqttConfig()
{
#define READ_STR_CONFIG_FROM_JSON(name) \
  if (strlen(doc[#name]))               \
  strlcpy(config.name, doc[#name], sizeof(config.name))

#define READ_INT_CONFIG_FROM_JSON(name) \
  if (doc[#name])                       \
  config.name = doc[#name]

  LOGD("\n");

  StaticJsonDocument<512> doc;

  if (!SPIFFS.begin())
  {
    LOGD("[ERROR] Mount spiffs failed.");
    return;
  }

  File f = SPIFFS.open("/mqtt.json", "r");
  if (!f)
  {
    LOGD("[ERROR] Config file not exist.");
    SPIFFS.end();
    return;
  }

  DeserializationError error = deserializeJson(doc, f);
  if (error)
  {
    LOGD("[ERROR] Failed to read file, using default configuration.");
    SPIFFS.end();
    return;
  }

  READ_STR_CONFIG_FROM_JSON(mqttServer);
  READ_STR_CONFIG_FROM_JSON(mqttPort);
  READ_STR_CONFIG_FROM_JSON(mqttUser);
  READ_STR_CONFIG_FROM_JSON(mqttPass);
  READ_STR_CONFIG_FROM_JSON(mqttSubTopic);
  serializeJson(doc, *DBGCOM);
  SPIFFS.end();
}

/**
 * @description: Web设置wifi界面点击保存的回调函数
 */
void saveConfigCallback()
{
  LOGD("Should save config");
  isSaveConfig = true;
}

/**
 * @description: 配置灯的颜色
 * @param color：LedColor
 */
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

/**
 * @description: 设置灯带中某一个灯的颜色
 * @param index：灯的下标，从0开始
 * @param R：红 0~255
 * @param G：绿 0~255
 * @param B：蓝 0~255
 */
void singleLedColor(int index, int R, int G, int B)
{
  WS2812B.setPixelColor(index, (((R & 0xffffff) << 16) | ((G & 0xffffff) << 8) | B));
  WS2812B.show();
}

/**
 * @description: 复位8266模组
 */
void resetESP8266()
{
  ESP.reset();
}

/**
 * @description: 复位E18模块
 */
void resetZigbee()
{
  digitalWrite(ZGB_RST_PIN, LOW);
  delay(500);
  digitalWrite(ZGB_RST_PIN, HIGH);
  LOGD("Reset zigbee");
}

/**
 * @description：检查网络状态，断网亮红灯，重新联网亮绿灯
 */
void checkNetwork()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    //通过标志位，loop期间断网不要重复设置灯红色，只要设置一次即可，网络恢复同理也不要灭灯多次
    if (isWiFiConnected)
    {
      isWiFiConnected = false;
      ledShowColor(LedColorRed);
      LOGD("[ERROR] Wifi disconnect");
      //开启定时器，10分钟内没连网成功则重启
      timer.once(60 * 10, resetESP8266);
    }
  }
  else
  {
    if (!isWiFiConnected)
    {
      ledShowColor(LedColorGreen);
      LOGD("Wifi connect");
      timer.detach();
    }
    isWiFiConnected = true;
  }
}

/**
 * @description：检查 tcp client(zigbee2mqtt node)的连接状态，已断开的移除，新连接的加到数组
 */
void checkTcpClient()
{
  //先把所有断开的客户端移除
  for (byte i = 0; i < MAX_NMEA_CLIENTS; i++)
  {
    if (TCPClient[i] && !TCPClient[i]->connected())
    {
      TCPClient[i]->stop();
      delete TCPClient[i];
      TCPClient[i] = NULL;
      LOGD("[ERROR] Client disconnected 1");
      ledShowColor(LedColorGreen);
      resetZigbee(); // node断开时复位一下 zigbee，避免node再连上时候无法握手通信
    }
  }

  //如果有新客户端进来，读取过 server->available() 后，这个 hasClient 就不会为true了，直到有新的客户端连进来
  if (server->hasClient())
  {
    LOGD("server->hasClient");
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
          LOGD("[ERROR] Client disconnected 2");
          ledShowColor(LedColorGreen);
          resetZigbee(); // node断开时复位一下 zigbee，避免node再连上时候无法握手通信
        }
        /*
        堆上开辟对象内存，指针指向它，然后把新进客户端的成员属性赋值给它，对象内存还是2个，只是成员变量值相同
        为什么用指针？直接2个对象赋值不就好了
        */
        TCPClient[i] = new WiFiClient;
        *TCPClient[i] = server->available();
        LOGD("New client for COM");
        ledShowColor(LedColorBlue);
      }
    }
    //遍历之后，如果之前数组里的客户端已满，并且都是连接的状态，无法接受新的客户端，则把新客户端读走并 stop
    WiFiClient tmpServerClient = server->available();
    if (tmpServerClient)
    {
      tmpServerClient.stop();
      LOGD("[ERROR] Client's array is full, stop new coming");
    }
    else
    {
      LOGD("All clients are accepted");
    }
  }
}

/**
 * @description: 读取 tcp client 发来的数据，然后通过 串口0-tx 发给E18
 */
void wifi2Zigbee()
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
        DBGCOM->printf("W->Z(%d):\t", i1);
        for (int i = 0; i < i1; i++)
          DBGCOM->printf("%02x ", buf1[i]);
        DBGCOM->println();
#endif
        i1 = 0;
      }
    }
  }
}

/**
 * @description: 读取E18发来的数据(UART0-RX)，发给 tcp client
 */
void zigbee2Wifi()
{
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
      DBGCOM->printf("Z->W(%d):\t", i2);
      for (int i = 0; i < i2; i++)
        DBGCOM->printf("%02x ", buf2[i]);
      DBGCOM->println();
#endif
      i2 = 0;
    }
  }
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @description: setup
 */
void setup()
{
  //配置LED灯，未联网红色、联网绿色、node连进来蓝色
  WS2812B.begin();
  WS2812B.clear();
  WS2812B.setBrightness(130);
  ledShowColor(LedColorRed);

  /*
  拨盘开关引脚配置
  opt3：USB TTY <==> ESP 8266 debug port UART1.  ESP 8266 TTY UART0 <==> E18 TTY
  */
  pinMode(TTY_SEL0_PIN, OUTPUT);
  digitalWrite(TTY_SEL0_PIN, LOW);
  pinMode(TTY_SEL1_PIN, OUTPUT);
  digitalWrite(TTY_SEL1_PIN, HIGH);
  delay(100);

  //通讯串口0和调试串口1初始化
  COM->begin(UART_BAUD0, SERIAL_PARAM0);
#if (DEBUG)
  DBGCOM->begin(115200);
#endif

  //从文件系统中读取mqtt的配置参数，以便带到web配网界面中显示
  getMqttConfig();
  WiFiManagerParameter custom_mqtt_server("server", "mqtt_server", config.mqttServer, sizeof(config.mqttServer));
  WiFiManagerParameter custom_mqtt_port("port", "mqtt_port", config.mqttPort, sizeof(config.mqttPort));
  WiFiManagerParameter custom_mqtt_user("user", "mqtt_user", config.mqttUser, sizeof(config.mqttUser));
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt_pass", config.mqttPass, sizeof(config.mqttPass));
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt_topic", config.mqttSubTopic, sizeof(config.mqttSubTopic));

  /*
  配置 WIFI，在 loop 中断网，网络恢复后会自动重连上，不需要用 if (WiFi.status() == WL_CONNECTED) 去判断网络和做重连的操作
  上次如果有配置过wifi账号密码，启动后autoConnect如果连接不成功会等待10s，10s内把路由那边的wifi密码开起来还能连上不会自动变AP，
  不设这个超时连接失败是立刻变AP
  */
  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(10);
  // randomSeed(analogRead(15));
  // String apName = String("ZigBeeAP_") + String(random(100, 999)); //先不用随机数了，使用芯片id做ap和host的名字
  String apName = String("ZigBeeGateWay_") + String(ESP.getChipId());
  wifiManager.setHostname(apName);
  LOGD("\n\n Begin connect wifi, apName = %s", apName.c_str());

  //配置Web设置wifi界面点击保存的回调函数，以及添加mqtt web配置菜单项
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_mqtt_topic);

  //开启定时器，10分钟内没连网成功则重启
  timer.once(60 * 10, resetESP8266);

  //网络设置界面exit会走返回这个false。web界面wifi密码连接错误此方法不会返回，还是重新变回ap
  if (!wifiManager.autoConnect(apName.c_str()))
  {
    LOGD("[ERROR] Failed to connect, now reset");
    delay(100);
    ESP.reset();
  }

  //点击保存，连网成功后才保存mqtt配置，不是点击保存时就保存。且如果上电没有走UI配网流程直接连网了也不保存。
  if (isSaveConfig)
  {
    LOGD("Saving config");
    strlcpy(config.mqttServer, custom_mqtt_server.getValue(), sizeof(config.mqttServer));
    strlcpy(config.mqttPort, custom_mqtt_port.getValue(), sizeof(config.mqttPort));
    strlcpy(config.mqttUser, custom_mqtt_user.getValue(), sizeof(config.mqttUser));
    strlcpy(config.mqttPass, custom_mqtt_pass.getValue(), sizeof(config.mqttPass));
    strlcpy(config.mqttSubTopic, custom_mqtt_topic.getValue(), sizeof(config.mqttSubTopic));

    DynamicJsonDocument doc(512);
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttPass"] = config.mqttPass;
    doc["mqttSubTopic"] = config.mqttSubTopic;

    if (SPIFFS.begin())
    {
      File configFile = SPIFFS.open("/mqtt.json", "w");
      if (configFile)
      {
        serializeJson(doc, *DBGCOM);
        serializeJson(doc, configFile);
        configFile.close();
      }
      else
      {
        LOGD("[ERROR] Failed to open config file for writing");
      }
      SPIFFS.end();
    }
    else
    {
      LOGD("[ERROR] Mount spiffs failed.");
    }
  }

  //开启 TCP server
  timer.detach();
  isWiFiConnected = true;
  ledShowColor(LedColorGreen);
  LOGD("TCP server: %s:%d", WiFi.localIP().toString().c_str(), TCP_PORT);
  static WiFiServer server_0(TCP_PORT);
  server = &server_0;
  server->begin();
  server->setNoDelay(true);

  // 8266复位也同时复位一下zigbee
  pinMode(ZGB_RST_PIN, OUTPUT);
  digitalWrite(ZGB_RST_PIN, HIGH);
  delay(100);
  resetZigbee();
}

/**
 * @description: loop
 */
void loop()
{
  //检查网络状态
  checkNetwork();

  //检查 tcp client(zigbee2mqtt node)的连接状态，已断开的移除，新连接的加到数组
  checkTcpClient();

  //读取 tcp client 发来的数据，然后通过 串口0-tx 发给E18
  wifi2Zigbee();

  //读取E18发来的数据(UART0-RX)，发给 tcp client
  zigbee2Wifi();
}
