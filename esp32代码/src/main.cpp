#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ArduinoJson.h>
#include <base64.h> // 使用ESP32 Arduino核心提供的base64库

#define MQTT_MAX_PACKET_SIZE 100000
#define MQTT_KEEPALIVE 30

#define CAMERA_MODEL_AI_THINKER // Has PSRAM

#include "camera_pins.h"

// Base64编码表
const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64编码函数
String base64_encode(const uint8_t *data, size_t length)
{
  String encoded = "";
  int i = 0;
  int j = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];

  while (length--)
  {
    char_array_3[i++] = *(data++);
    if (i == 3)
    {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++)
        encoded += b64_alphabet[char_array_4[i]];
      i = 0;
    }
  }

  if (i)
  {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; j < i + 1; j++)
      encoded += b64_alphabet[char_array_4[j]];

    while (i++ < 3)
      encoded += '=';
  }

  return encoded;
}

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "liqiu";
const char *password = "asdfghjk";

// ===========================
// MQTT配置
// ===========================
const char *mqtt_server = "n09f9099.ala.cn-hangzhou.emqxsl.cn";
const int mqtt_port = 8883;
const char *mqtt_username = "znnyesp32";
const char *mqtt_password = "znnyesp32";
const char *mqtt_client_id = "ESP32CAM-Client";
const char *mqtt_topic = "esp32cam/heartbeat";
const char *mqtt_sensor_topic = "esp32cam/sensor";         // 传感器数据主题
const char *mqtt_status_topic = "esp32cam/status";         // 设备状态主题
const char *mqtt_control_topic = "esp32cam/control";       // 控制命令主题
const char *mqtt_image_topic = "esp32cam/image";           // 图像数据主题
const char *mqtt_image_info_topic = "esp32cam/image/info"; // 图像信息主题
const char *mqtt_image_data_topic = "esp32cam/image/data"; // 图像数据主题

// MQTT客户端
WiFiClientSecure espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
unsigned long lastImageSent = 0; // 上次发送图像的时间

static const char ca_cert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";

// 设备状态变量
static uint8_t fanStatus = 0;     // 风扇状态 (0: 关, 1: 开)
static uint8_t exhaustStatus = 0; // 排气扇状态 (0: 关, 1: 开)
static uint8_t pumpStatus = 0;    // 水泵状态 (0: 关, 1: 开)

// 传感器数据变量
String waterLevel = "";
String temperature = "";
String humidity = "";
String lightLevel = "";

// 串口数据处理
String serialBuffer = "";
bool newData = false;

void startCameraServer();
void setupLedFlash(int pin);
void setupMQTT();
void reconnect();
void callback(char *topic, byte *payload, unsigned int length);
void processSerialData();
void parseJsonData(String jsonStr);
void publishSensorData();
void captureAndSendImage();

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  // config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    if (psramFound())
    {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  }
  else
  {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);       // flip it back
    s->set_brightness(s, 1);  // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    s->set_framesize(s, FRAMESIZE_QQVGA); // 设置为QQVGA尺寸 (160x120)
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // 设置MQTT
  setupMQTT();

  startCameraServer();

  // Serial.print("Camera Ready! Use 'http://");
  // Serial.print(WiFi.localIP());
  // Serial.println("' to connect");
}

void loop()
{
  // 检查MQTT连接状态
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  // 处理串口数据
  processSerialData();

  // 如果有新数据，解析并发布
  if (newData)
  {
    newData = false;
    parseJsonData(serialBuffer);
    publishSensorData();
  }

  // 每10秒发送一次心跳包
  unsigned long now = millis();
  if (now - lastMsg > 10000)
  {
    lastMsg = now;
    String heartbeat = "ESP32CAM heartbeat: " + String(now);
    // Serial.println("发送心跳: " + heartbeat);
    client.publish(mqtt_topic, heartbeat.c_str());
  }

  // 每5秒发送一次图像
  if (now - lastImageSent > 5000)
  {
    lastImageSent = now;
    captureAndSendImage();
  }

  delay(100);
}

// 捕获并发送图像
void captureAndSendImage()
{
  // 获取相机传感器
  sensor_t *s = esp_camera_sensor_get();
  if (!s)
  {
    Serial.println("无法获取相机传感器，请检查初始化");
    return;
  }

  // 使用最小的分辨率 QQVGA (160x120)
  s->set_framesize(s, FRAMESIZE_HVGA);
  // 提高压缩率，降低图像质量以减小数据大小
  s->set_quality(s, 12); // 值范围通常为 0-63，值越小质量越低
  delay(100);            // 给相机一些时间适应新设置

  // 检查PSRAM
  if (!psramFound())
  {
    Serial.println("警告：未找到PSRAM，高分辨率图像可能无法获取");
  }

  // 尝试捕获图像，最多尝试3次
  camera_fb_t *fb = NULL;
  int retryCount = 0;
  while (!fb && retryCount < 3)
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.print("图像捕获失败，尝试第 ");
      Serial.print(retryCount + 1);
      Serial.println(" 次");
      retryCount++;
      delay(500); // 延迟500ms后重试
    }
  }

  if (!fb)
  {
    Serial.println("图像捕获最终失败，请检查相机硬件连接或降低分辨率");
    return;
  }

  Serial.print("图像捕获成功，大小: ");
  Serial.println(fb->len);

  // 检查MQTT连接状态
  if (client.connected())
  {
    // 先发送图像信息
    StaticJsonDocument<256> infoDoc;
    infoDoc["format"] = "jpeg";
    infoDoc["width"] = fb->width;
    infoDoc["height"] = fb->height;
    infoDoc["timestamp"] = millis();
    infoDoc["size"] = fb->len;

    String infoJson;
    serializeJson(infoDoc, infoJson);

    if (client.publish(mqtt_image_info_topic, infoJson.c_str()))
    {
      Serial.println("图像信息发送成功");

      // 使用ESP32内置base64库进行编码
      String base64Image = base64::encode(fb->buf, fb->len);

      // 直接发送base64编码的图像数据
      if (client.publish(mqtt_image_data_topic, base64Image.c_str()))
      {
        Serial.println("图像数据发送成功");
      }
      else
      {
        Serial.println("图像数据发送失败，可能是数据太大");
        Serial.print("Base64编码后数据大小: ");
        Serial.println(base64Image.length());
      }
    }
    else
    {
      Serial.println("图像信息发送失败");
    }
  }
  else
  {
    Serial.println("MQTT未连接，无法发送图像");
  }

  // 释放图像缓冲区
  esp_camera_fb_return(fb);
}

// 处理串口数据
void processSerialData()
{
  while (Serial.available() > 0)
  {
    char inChar = (char)Serial.read();

    // 将字符添加到缓冲区
    serialBuffer += inChar;

    // 如果收到换行符或回车符，表示一条完整消息
    if (inChar == '\n' || inChar == '\r')
    {
      // 去除前后空白字符
      serialBuffer.trim();

      // 检查是否是JSON格式数据
      if (serialBuffer.length() > 0)
      {
        if (serialBuffer.startsWith("{") && serialBuffer.endsWith("}"))
        {
          // Serial.println("检测到有效JSON数据");
          newData = true;
        }
        else
        {
          // 不是JSON格式，但仍打印出来以便调试
          // Serial.print("收到无效数据: ");
          // Serial.println(serialBuffer);
          // 清空缓冲区
          serialBuffer = "";
        }
      }
      else
      {
        // 空白行，忽略
        serialBuffer = "";
      }
    }

    // 如果缓冲区过长，防止溢出
    if (serialBuffer.length() > 512)
    {
      Serial.println("串口缓冲区溢出，清空数据");
      serialBuffer = "";
    }
  }
}

// 解析JSON数据
void parseJsonData(String jsonStr)
{
  // Serial.print("正在解析JSON: ");
  // Serial.println(jsonStr);

  // 创建JSON缓冲区
  StaticJsonDocument<256> doc;

  // 解析JSON字符串
  DeserializationError error = deserializeJson(doc, jsonStr);

  // 如果解析成功
  if (!error)
  {
    // 提取传感器数据
    if (doc.containsKey("water"))
      waterLevel = doc["water"].as<String>();

    if (doc.containsKey("temp"))
      temperature = doc["temp"].as<String>();

    if (doc.containsKey("humi"))
      humidity = doc["humi"].as<String>();

    if (doc.containsKey("light"))
      lightLevel = doc["light"].as<String>();

    // 更新设备状态
    if (doc.containsKey("fan"))
    {
      String fanStr = doc["fan"].as<String>();
      fanStatus = (fanStr == "on") ? 1 : 0;
    }

    if (doc.containsKey("exh"))
    {
      String exhStr = doc["exh"].as<String>();
      exhaustStatus = (exhStr == "on") ? 1 : 0;
    }

    if (doc.containsKey("pum"))
    {
      String pumStr = doc["pum"].as<String>();
      pumpStatus = (pumStr == "on") ? 1 : 0;
    }

    // Serial.println("JSON解析成功");

    // // 打印解析后的数据，用于调试
    // Serial.println("解析结果:");
    // Serial.print("水位: ");
    // Serial.println(waterLevel);
    // Serial.print("温度: ");
    // Serial.println(temperature);
    // Serial.print("湿度: ");
    // Serial.println(humidity);
    // Serial.print("光照: ");
    // Serial.println(lightLevel);
    // Serial.print("风扇: ");
    // Serial.println(fanStatus ? "on" : "off");
    // Serial.print("排气: ");
    // Serial.println(exhaustStatus ? "on" : "off");
    // Serial.print("水泵: ");
    // Serial.println(pumpStatus ? "on" : "off");
  }
  else
  {
    Serial.print("JSON解析失败: ");
    Serial.println(error.c_str());
  }

  // 清空缓冲区
  serialBuffer = "";
}

// 发布传感器数据到MQTT
void publishSensorData()
{
  // 创建传感器数据JSON
  StaticJsonDocument<256> sensorDoc;
  sensorDoc["water"] = waterLevel;
  sensorDoc["temp"] = temperature;
  sensorDoc["humi"] = humidity;
  sensorDoc["light"] = lightLevel;

  // 创建设备状态JSON
  StaticJsonDocument<128> statusDoc;
  statusDoc["fan"] = fanStatus ? "on" : "off";
  statusDoc["exh"] = exhaustStatus ? "on" : "off";
  statusDoc["pum"] = pumpStatus ? "on" : "off";

  // 序列化JSON到字符串
  String sensorJson, statusJson;
  serializeJson(sensorDoc, sensorJson);
  serializeJson(statusDoc, statusJson);

  // 发布到MQTT
  if (client.connected())
  {
    // 发布传感器数据
    if (client.publish(mqtt_sensor_topic, sensorJson.c_str()))
    {
      Serial.println("传感器数据发布成功");
    }
    else
    {
      Serial.println("传感器数据发布失败");
    }

    // 发布设备状态
    if (client.publish(mqtt_status_topic, statusJson.c_str()))
    {
      Serial.println("设备状态发布成功");
    }
    else
    {
      Serial.println("设备状态发布失败");
    }
  }
}

// 设置MQTT
void setupMQTT()
{
  // 设置SSL/TLS证书
  espClient.setCACert(ca_cert);

  // 设置MQTT服务器和回调
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // 设置缓冲区大小
  client.setBufferSize(MQTT_MAX_PACKET_SIZE);

  // 首次连接
  reconnect();
}

// 重新连接MQTT
void reconnect()
{
  // 尝试连接，直到成功
  while (!client.connected())
  {
    Serial.print("尝试MQTT连接...");
    // 尝试连接
    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password))
    {
      Serial.println("已连接");
      // 连接成功后发布连接消息
      client.publish(mqtt_topic, "ESP32CAM已连接");
      // 订阅控制主题
      client.subscribe(mqtt_control_topic);
      Serial.print("已订阅主题: ");
      Serial.println(mqtt_control_topic);
    }
    else
    {
      Serial.print("连接失败, rc=");
      Serial.print(client.state());
      Serial.println(" 5秒后重试");
      // 等待5秒再试
      delay(5000);
    }
  }
}

// MQTT回调函数
void callback(char *topic, byte *payload, unsigned int length)
{
  // Serial.print("收到消息 [");
  // Serial.print(topic);
  // Serial.print("] ");

  // 将接收到的消息转为字符串
  String message;
  for (int i = 0; i < length; i++)
  {
    message += (char)payload[i];
    // Serial.print((char)payload[i]);
  }
  // Serial.println();

  // 检查是否是控制主题
  if (strcmp(topic, mqtt_control_topic) == 0)
  {
    // Serial.println("收到控制消息，转发至串口");

    // 解析JSON控制消息
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (!error)
    {
      // 创建要发送至串口的JSON对象
      StaticJsonDocument<128> controlDoc;

      // 提取风扇控制
      if (doc.containsKey("fan"))
      {
        String fanState = doc["fan"].as<String>();
        controlDoc["fan"] = fanState;
      }

      // 提取排气扇控制
      if (doc.containsKey("exh"))
      {
        String exhState = doc["exh"].as<String>();
        controlDoc["exh"] = exhState;
      }

      // 提取水泵控制
      if (doc.containsKey("pum"))
      {
        String pumState = doc["pum"].as<String>();
        controlDoc["pum"] = pumState;
      }

      // 将控制命令转为JSON字符串并发送至串口
      String controlJson;
      serializeJson(controlDoc, controlJson);

      // Serial.print("发送控制命令至串口: ");
      // Serial.println(controlJson);

      // 发送至串口，结尾添加换行符
      Serial.print(controlJson + "\r\n");
    }
    else
    {
      Serial.print("控制消息JSON解析失败: ");
      Serial.println(error.c_str());
    }
  }
}
