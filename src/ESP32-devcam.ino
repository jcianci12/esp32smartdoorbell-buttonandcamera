#include "OV2640.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <AutoWifi.h>

#include <WiFi.h>
#include <HTTPClient.h>

#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "OV2640.h"
#include "CRtspSession.h"

#include "PubSubClient.h"

/////////////////////////////////////////////////////////////////

#include "Button2.h"

/////////////////////////////////////////////////////////////////

#define BUTTON_A_PIN 2

/////////////////////////////////////////////////////////////////

// This board has slightly different GPIO bindings (and lots more RAM)
// uncomment to use
//#define USEBOARD_TTGO_T

// #define USEBOARD_AITHINKER

#ifndef USEBOARD_AITHINKER
#define ENABLE_OLED //if want use oled ,turn on thi macro
#endif

// #define SOFTAP_MODE // If you want to run our own softap turn this on
//#define ENABLE_WEBSERVER
#define ENABLE_RTSPSERVER

#define ENABLE_MQTT

#ifndef USEBOARD_AITHINKER
// If your board has a GPIO which is attached to a button, uncomment the following line
// and adjust the GPIO number as needed.  If that button is held down during boot the device
// will factory reset.
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
bool hasDisplay; // we probe for the device at runtime
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
/////////////////////////////////////////////////////////////////////////
Button2 buttonA = Button2(DOORBELL_BUTTON);
// variables will change:
/////////////////////////////////////////////////////////////////////////

int period = 1000;
unsigned long time_now = 0;

int32_t i = 0;

#ifdef ENABLE_MQTT
const char *mqttServer = "192.168.5.44";
const int mqttPort = 1883;
const char *mqttUser = "mqtt-user";
const char *mqttPassword = "Chooks12$";
WiFiClient espClient;
PubSubClient pubsubclient(espClient);
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
TaskHandle_t Core1Task;

void setup()
{

    // xTaskCreatePinnedToCore(
    //     Core0Code,   /* Function to implement the task */
    //     "Task1",     /* Name of the task */
    //     10000,       /* Stack size in words */
    //     NULL,        /* Task input parameter */
    //     0,           /* Priority of the task */
    //     &Core0Task1, /* Task handle. */
    //     0);          /* Core where the task should run */

    // xTaskCreatePinnedToCore(
    //     Core1Code, /* Function to implement the task */
    //     "Task1",   /* Name of the task */
    //     10000,     /* Stack size in words */
    //     NULL,      /* Task input parameter */
    //     0,         /* Priority of the task */
    //     &Core1Task,    /* Task handle. */
    //     1);        /* Core where the task should run */

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

    ////////////////////////////////////////////////////////////////////////////////
    // initialize the pushbutton pin as an input:
    // buttonA.setChangedHandler(changed);
    buttonA.setPressedHandler(pressed);
    // buttonA.setReleasedHandler(released);

    // buttonA.setTapHandler(tap);
    // buttonA.setClickHandler(click);
    // buttonA.setLongClickHandler(longClick);
    // buttonA.setDoubleClickHandler(doubleClick);
    // buttonA.setTripleClickHandler(tripleClick);
    ////////////////////////////////////////////////////////////////////////////////////
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
    // WiFi.hostname(hostname); // FIXME - find out why undefined
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
    if (!digitalRead(FACTORYRESET_BUTTON)) // 1 means not pressed
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

#ifdef ENABLE_MQTT

    pubsubclient.setServer(mqttServer, mqttPort);
    while (!pubsubclient.connected())
    {
        Serial.println("Connecting to MQTT...");

        if (pubsubclient.connect("ESP32Client", mqttUser, mqttPassword))
        {

            Serial.println("connected");
        }
        else
        {

            Serial.print("failed with state ");
            Serial.print(pubsubclient.state());
            delay(2000);
        }
    }
    pubsubclient.publish("esp/test", "Hello from ESP32");

#endif
}

CStreamer *streamer;
CRtspSession *session;
WiFiClient client; // FIXME, support multiple clients

void loop()
{
#ifdef ENABLE_WEBSERVER
    server.handleClient();
#endif

#ifdef ENABLE_RTSPSERVER
    rtspServer.begin();
#endif
    buttonA.loop();
}

void Core0Code(void *parameter)
{
    for (;;)
    {
        buttonA.loop();
        if (millis() >= time_now + period + 1)
        {
            time_now += period;
            Serial.println("core 0 loop");
        }
        vTaskDelay(20);
    }
}
void message()
{
    Serial.println("Fetching");
#ifdef ENABLE_MQTT
    bool MQTT_Reply = pubsubclient.publish("test", "Hello from ESP8266");
    if (MQTT_Reply = true)
    {
        Serial.println("publish success");
    }
    else
    {
        Serial.println("Publish failure");
    }
#endif
}

/////////////////////////////////////////////////////////////////

void pressed(Button2 &btn)
{
    Serial.println("pressed");
    message();
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
/////////////////////////////////////////////////////////////////