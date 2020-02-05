# 1 "C:\\Users\\jon\\AppData\\Local\\Temp\\tmpdkh9dh1u"
#include <Arduino.h>
# 1 "C:/Users/jon/Desktop/IT_Projects/arduino/Smart Doorbell Camera and Button/src/ESP32-devcam.ino"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <AutoWifi.h>

#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "OV2640.h"
#include "CRtspSession.h"



#include "Button2.h"



#define BUTTON_A_PIN 2
# 27 "C:/Users/jon/Desktop/IT_Projects/arduino/Smart Doorbell Camera and Button/src/ESP32-devcam.ino"
#ifndef USEBOARD_AITHINKER
#define ENABLE_OLED 
#endif


#define ENABLE_WEBSERVER 
#define ENABLE_RTSPSERVER 

#ifndef USEBOARD_AITHINKER



#define FACTORYRESET_BUTTON 32
#endif
#define DOORBELL_BUTTON 4

#ifdef ENABLE_OLED
#include "SSD1306.h"
#define OLED_ADDRESS 0x3c

#ifdef USEBOARD_TTGO_T
#define I2C_SDA 21
#define I2C_SCL 22
#else
#define I2C_SDA 14
#define I2C_SCL 13
#endif
SSD1306Wire display(OLED_ADDRESS, I2C_SDA, I2C_SCL, GEOMETRY_128_32);
bool hasDisplay;
#endif

OV2640 cam;

#ifdef ENABLE_WEBSERVER
WebServer server(80);
#endif

#ifdef ENABLE_RTSPSERVER
WiFiServer rtspServer(8554);
#endif

#ifdef SOFTAP_MODE
IPAddress apIP = IPAddress(192, 168, 1, 1);
#else
#endif

Button2 buttonA = Button2(DOORBELL_BUTTON);



int period = 1000;
unsigned long time_now = 0;

int32_t i = 0;



#include <esp_now.h>

#define SENDER 

#ifndef SENDER
#define RECEIVER 
#endif
static uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
typedef struct __attribute__((packed)) esp_now_msg_t
{
  uint32_t address;
  uint32_t counter;

} esp_now_msg_t;
static void handle_error(esp_err_t err);
static void msg_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);
static void msg_send_cb(const uint8_t *mac, esp_now_send_status_t sendStatus);
static void send_msg(esp_now_msg_t *msg);
static void network_setup(void);
void handle_jpg_stream(void);
void handle_jpg(void);
void handleNotFound();
void lcdMessage(String msg);
void setup();
void message();
void loop();
void Core0Code(void *parameter);
void pressed(Button2 &btn);
void released(Button2 &btn);
void changed(Button2 &btn);
void click(Button2 &btn);
void longClick(Button2 &btn);
void doubleClick(Button2 &btn);
void tripleClick(Button2 &btn);
void tap(Button2 &btn);
#line 99 "C:/Users/jon/Desktop/IT_Projects/arduino/Smart Doorbell Camera and Button/src/ESP32-devcam.ino"
static void handle_error(esp_err_t err)
{
  switch (err)
  {
  case ESP_ERR_ESPNOW_NOT_INIT:
    Serial.println("Not init");
    break;

  case ESP_ERR_ESPNOW_ARG:
    Serial.println("Argument invalid");
    break;

  case ESP_ERR_ESPNOW_INTERNAL:
    Serial.println("Internal error");
    break;

  case ESP_ERR_ESPNOW_NO_MEM:
    Serial.println("Out of memory");
    break;

  case ESP_ERR_ESPNOW_NOT_FOUND:
    Serial.println("Peer is not found");
    break;

  case ESP_ERR_ESPNOW_IF:
    Serial.println("Current WiFi interface doesn't match that of peer");
    break;

  default:
    break;
  }
}

static void msg_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
  if (len == sizeof(esp_now_msg_t))
  {
    esp_now_msg_t msg;
    memcpy(&msg, data, len);

    Serial.print("Counter: ");
    Serial.println(msg.counter);

  }
}

static void msg_send_cb(const uint8_t *mac, esp_now_send_status_t sendStatus)
{

  switch (sendStatus)
  {
  case ESP_NOW_SEND_SUCCESS:
    Serial.println("Send success");
    break;

  case ESP_NOW_SEND_FAIL:
    Serial.println("Send Failure");
    break;

  default:
    break;
  }
}

static void send_msg(esp_now_msg_t *msg)
{

  uint16_t packet_size = sizeof(esp_now_msg_t);
  uint8_t msg_data[packet_size];
  memcpy(&msg_data[0], msg, sizeof(esp_now_msg_t));

  esp_err_t status = esp_now_send(broadcast_mac, msg_data, packet_size);
  if (ESP_OK != status)
  {
    Serial.println("Error sending message");
    handle_error(status);
  }
  Serial.println("Message sent!");
}

static void network_setup(void)
{




  if (esp_now_init() != 0)
  {
    return;
  }

  esp_now_peer_info_t peer_info;
  peer_info.channel = WiFi.channel();
  memcpy(peer_info.peer_addr, broadcast_mac, 6);
  peer_info.ifidx = ESP_IF_WIFI_STA;
  peer_info.encrypt = false;
  esp_err_t status = esp_now_add_peer(&peer_info);
  if (ESP_OK != status)
  {
    Serial.println("Could not add peer");
    handle_error(status);
  }


  status = esp_now_register_recv_cb(msg_recv_cb);
  if (ESP_OK != status)
  {
    Serial.println("Could not register callback");
    handle_error(status);
  }

  status = esp_now_register_send_cb(msg_send_cb);
  if (ESP_OK != status)
  {
    Serial.println("Could not register send callback");
    handle_error(status);
  }
}



#ifdef ENABLE_WEBSERVER
void handle_jpg_stream(void)
{

  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (1)
  {

    cam.run();



    i = i + 1;

    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);

    client.write((char *)cam.getfb(), cam.getSize());
    server.sendContent("\r\n");
    if (!client.connected())
      break;
  }
}
void handle_jpg(void)
{


  WiFiClient client = server.client();

  cam.run();
  if (!client.connected())
  {
    return;
  }


  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-disposition: inline; filename=capture.jpg\r\n";
  response += "Content-type: image/jpeg\r\n\r\n";
  server.sendContent(response);
  client.write((char *)cam.getfb(), cam.getSize());
}

void handleNotFound()
{
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text/plain", message);
}
#endif

void lcdMessage(String msg)
{
#ifdef ENABLE_OLED
  if (hasDisplay)
  {
    display.clear();
    display.drawString(128 / 2, 32 / 2, msg);
    display.display();
  }
#endif
}

TaskHandle_t Core0Task1;
TaskHandle_t Core1Task1;

void setup()
{

  xTaskCreatePinnedToCore(
      Core0Code,
      "Task0",
      9208,
      NULL,
      0,
      &Core0Task1,
      0);
# 318 "C:/Users/jon/Desktop/IT_Projects/arduino/Smart Doorbell Camera and Button/src/ESP32-devcam.ino"
#ifdef ENABLE_OLED
  hasDisplay = display.init();
  if (hasDisplay)
  {
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
  }
#endif
  lcdMessage("booting");

  Serial.begin(115200);
  while (!Serial)
  {
    ;
  }



  buttonA.setChangedHandler(changed);
  buttonA.setPressedHandler(pressed);
  buttonA.setReleasedHandler(released);

  buttonA.setTapHandler(tap);
  buttonA.setClickHandler(click);
  buttonA.setLongClickHandler(longClick);
  buttonA.setDoubleClickHandler(doubleClick);
  buttonA.setTripleClickHandler(tripleClick);

  int camInit =
#ifdef USEBOARD_TTGO_T
      cam.init(esp32cam_ttgo_t_config);
#else
#ifdef USEBOARD_AITHINKER
      cam.init(esp32cam_aithinker_config);
#else
      cam.init(esp32cam_config);
#endif
#endif
  Serial.printf("Camera init returned %d\n", camInit);

  IPAddress ip;

#ifdef SOFTAP_MODE
  const char *hostname = "devcam";

  lcdMessage("starting softAP");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  bool result = WiFi.softAP(hostname, "12345678", 1, 0);
  if (!result)
  {
    Serial.println("AP Config failed.");
    return;
  }
  else
  {
    Serial.println("AP Config Success.");
    Serial.print("AP MAC: ");
    Serial.println(WiFi.softAPmacAddress());

    ip = WiFi.softAPIP();
  }
#else

  WiFi.mode(WIFI_STA);

  AutoWifi a;

#ifdef FACTORYRESET_BUTTON
  pinMode(FACTORYRESET_BUTTON, INPUT);
  if (!digitalRead(FACTORYRESET_BUTTON))
    a.resetProvisioning();
#endif

  if (!a.isProvisioned())
    lcdMessage("Setup wifi!");
  else
    lcdMessage(String("join ") + a.getSSID());

  a.startWifi();

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(F("."));
  }
  ip = WiFi.localIP();
  Serial.println(F("WiFi connected"));
  Serial.println(ip);
#endif
  lcdMessage(ip.toString());

#ifdef ENABLE_WEBSERVER
  server.on("/", HTTP_GET, handle_jpg_stream);
  server.on("/jpg", HTTP_GET, handle_jpg);
  server.onNotFound(handleNotFound);
  server.begin();
#endif

#ifdef ENABLE_RTSPSERVER
  rtspServer.begin();
#endif


  network_setup();

}

CStreamer *streamer;
CRtspSession *session;
WiFiClient client;


void message(){
#ifdef SENDER
          static uint32_t counter = 0;
          esp_now_msg_t msg;
          msg.address = 0;
          msg.counter = ++counter;
          send_msg(&msg);

    #endif
}

void loop()
{

#ifdef ENABLE_WEBSERVER
  server.handleClient();
  if (millis() >= time_now + 100 + 1)
  {
    time_now += period;
    Serial.print("1");



    message();


  }
#endif
}
# 479 "C:/Users/jon/Desktop/IT_Projects/arduino/Smart Doorbell Camera and Button/src/ESP32-devcam.ino"
void Core0Code(void *parameter)
{
  for (;;)
  {
    buttonA.loop();
    if (millis() >= time_now + period + 1)
    {
      time_now += period;
      Serial.print(".0");
# 500 "C:/Users/jon/Desktop/IT_Projects/arduino/Smart Doorbell Camera and Button/src/ESP32-devcam.ino"
    }
    vTaskDelay(20);

  }
}



void pressed(Button2 &btn)
{
  Serial.println("pressed");
#ifdef SENDER
  static uint32_t counter = 0;
  esp_now_msg_t msg;
  msg.address = 0;
  msg.counter = ++counter;
  Serial.println("sending message");

  send_msg(&msg);

#endif
}
void released(Button2 &btn)
{
  Serial.print("released: ");
  Serial.println(btn.wasPressedFor());
}
void changed(Button2 &btn)
{
  Serial.println("changed");
}
void click(Button2 &btn)
{
  Serial.println("click\n");
  Serial.println(btn.wasPressedFor());
}
void longClick(Button2 &btn)
{
  Serial.println("long click\n");
}
void doubleClick(Button2 &btn)
{
  Serial.println("double click\n");
}
void tripleClick(Button2 &btn)
{
  Serial.println("triple click\n");
}
void tap(Button2 &btn)
{
  Serial.println("tap");
}