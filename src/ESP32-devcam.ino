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


#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// ESP32 has two cores: APPlication core and PROcess core (the one that runs ESP32 SDK stack)
#define APP_CPU 1
#define PRO_CPU 0

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
#define ENABLE_WEBSERVER
//#define ENABLE_RTSPSERVER

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


// ===== rtos task handles =========================
// Streaming is implemented with 3 tasks:
TaskHandle_t tMjpeg;   // handles client connections to the webserver
TaskHandle_t tCam;     // handles getting picture frames from the camera and storing them locally
TaskHandle_t tStream;  // actually streaming frames to all connected clients

// frameSync semaphore is used to prevent streaming buffer as it is replaced with the next frame
SemaphoreHandle_t frameSync = NULL;

// Queue stores currently connected clients to whom we are streaming
QueueHandle_t streamingClients;

// We will try to achieve 25 FPS frame rate
const int FPS = 14;

// We will handle web client requests every 50 ms (20 Hz)
const int WSINTERVAL = 100;


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

    // #ifdef ENABLE_RTSPSERVER
    //     rtspServer.begin();
    // #endif

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

    // Start mainstreaming RTOS task
    xTaskCreatePinnedToCore(
        mjpegCB,
        "mjpeg",
        4 * 1024,
        NULL,
        2,
        &tMjpeg,
        APP_CPU);
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
    //buttonA.loop();
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
    bool MQTT_Reply = pubsubclient.publish("doorbell", "on");
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


// ======== Server Connection Handler Task ==========================
void mjpegCB(void* pvParameters) {
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(WSINTERVAL);

  // Creating frame synchronization semaphore and initializing it
  frameSync = xSemaphoreCreateBinary();
  xSemaphoreGive( frameSync );

  // Creating a queue to track all connected clients
  streamingClients = xQueueCreate( 10, sizeof(WiFiClient*) );

  //=== setup section  ==================

  //  Creating RTOS task for grabbing frames from the camera
  xTaskCreatePinnedToCore(
    camCB,        // callback
    "cam",        // name
    4096,         // stacj size
    NULL,         // parameters
    2,            // priority
    &tCam,        // RTOS task handle
    APP_CPU);     // core

  //  Creating task to push the stream to all connected clients
  xTaskCreatePinnedToCore(
    streamCB,
    "strmCB",
    4 * 1024,
    NULL, //(void*) handler,
    2,
    &tStream,
    APP_CPU);

  //  Registering webserver handling routines
  server.on("/mjpeg/1", HTTP_GET, handleJPGSstream);
  server.on("/jpg", HTTP_GET, handleJPG);
  server.onNotFound(handleNotFound);

  //  Starting webserver
  server.begin();

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    server.handleClient();

    //  After every server client handling request, we let other tasks run and then pause
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}


// Commonly used variables:
volatile size_t camSize;    // size of the current frame, byte
volatile char* camBuf;      // pointer to the current frame


// ==== RTOS task to grab frames from the camera =========================
void camCB(void* pvParameters) {

  TickType_t xLastWakeTime;

  //  A running interval associated with currently desired frame rate
  const TickType_t xFrequency = pdMS_TO_TICKS(1000 / FPS);

  // Mutex for the critical section of swithing the active frames around
  portMUX_TYPE xSemaphore = portMUX_INITIALIZER_UNLOCKED;

  //  Pointers to the 2 frames, their respective sizes and index of the current frame
  char* fbs[2] = { NULL, NULL };
  size_t fSize[2] = { 0, 0 };
  int ifb = 0;

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    //  Grab a frame from the camera and query its size
    cam.run();
    size_t s = cam.getSize();

    //  If frame size is more that we have previously allocated - request  125% of the current frame space
    if (s > fSize[ifb]) {
      fSize[ifb] = s * 4 / 3;
      fbs[ifb] = allocateMemory(fbs[ifb], fSize[ifb]);
    }

    //  Copy current frame into local buffer
    char* b = (char*) cam.getfb();
    memcpy(fbs[ifb], b, s);

    //  Let other tasks run and wait until the end of the current frame rate interval (if any time left)
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    //  Only switch frames around if no frame is currently being streamed to a client
    //  Wait on a semaphore until client operation completes
    xSemaphoreTake( frameSync, portMAX_DELAY );

    //  Do not allow interrupts while switching the current frame
    portENTER_CRITICAL(&xSemaphore);
    camBuf = fbs[ifb];
    camSize = s;
    ifb++;
    ifb &= 1;  // this should produce 1, 0, 1, 0, 1 ... sequence
    portEXIT_CRITICAL(&xSemaphore);

    //  Let anyone waiting for a frame know that the frame is ready
    xSemaphoreGive( frameSync );

    //  Technically only needed once: let the streaming task know that we have at least one frame
    //  and it could start sending frames to the clients, if any
    xTaskNotifyGive( tStream );

    //  Immediately let other (streaming) tasks run
    taskYIELD();

    //  If streaming task has suspended itself (no active clients to stream to)
    //  there is no need to grab frames from the camera. We can save some juice
    //  by suspedning the tasks
    if ( eTaskGetState( tStream ) == eSuspended ) {
      vTaskSuspend(NULL);  // passing NULL means "suspend yourself"
    }
  }
}


// ==== Memory allocator that takes advantage of PSRAM if present =======================
char* allocateMemory(char* aPtr, size_t aSize) {

  //  Since current buffer is too smal, free it
  if (aPtr != NULL) free(aPtr);


  size_t freeHeap = ESP.getFreeHeap();
  char* ptr = NULL;

  // If memory requested is more than 2/3 of the currently free heap, try PSRAM immediately
  if ( aSize > freeHeap * 2 / 3 ) {
    if ( psramFound() && ESP.getFreePsram() > aSize ) {
      ptr = (char*) ps_malloc(aSize);
    }
  }
  else {
    //  Enough free heap - let's try allocating fast RAM as a buffer
    ptr = (char*) malloc(aSize);

    //  If allocation on the heap failed, let's give PSRAM one more chance:
    if ( ptr == NULL && psramFound() && ESP.getFreePsram() > aSize) {
      ptr = (char*) ps_malloc(aSize);
    }
  }

  // Finally, if the memory pointer is NULL, we were not able to allocate any memory, and that is a terminal condition.
  if (ptr == NULL) {
    ESP.restart();
  }
  return ptr;
}

// ==== STREAMING ======================================================
const char HEADER[] = "HTTP/1.1 200 OK\r\n" \
                      "Access-Control-Allow-Origin: *\r\n" \
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);


// ==== Handle connection request from clients ===============================
void handleJPGSstream(void)
{
  //  Can only acommodate 10 clients. The limit is a default for WiFi connections
  if ( !uxQueueSpacesAvailable(streamingClients) ) return;


  //  Create a new WiFi Client object to keep track of this one
  WiFiClient* client = new WiFiClient();
  *client = server.client();

  //  Immediately send this client a header
  client->write(HEADER, hdrLen);
  client->write(BOUNDARY, bdrLen);

  // Push the client to the streaming queue
  xQueueSend(streamingClients, (void *) &client, 0);

  // Wake up streaming tasks, if they were previously suspended:
  if ( eTaskGetState( tCam ) == eSuspended ) vTaskResume( tCam );
  if ( eTaskGetState( tStream ) == eSuspended ) vTaskResume( tStream );
}


// ==== Actually stream content to all connected clients ========================
void streamCB(void * pvParameters) {
  char buf[16];
  TickType_t xLastWakeTime;
  TickType_t xFrequency;

  //  Wait until the first frame is captured and there is something to send
  //  to clients
  ulTaskNotifyTake( pdTRUE,          /* Clear the notification value before exiting. */
                    portMAX_DELAY ); /* Block indefinitely. */

  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    // Default assumption we are running according to the FPS
    xFrequency = pdMS_TO_TICKS(1000 / FPS);

    //  Only bother to send anything if there is someone watching
    UBaseType_t activeClients = uxQueueMessagesWaiting(streamingClients);
    if ( activeClients ) {
      // Adjust the period to the number of connected clients
      xFrequency /= activeClients;

      //  Since we are sending the same frame to everyone,
      //  pop a client from the the front of the queue
      WiFiClient *client;
      xQueueReceive (streamingClients, (void*) &client, 0);

      //  Check if this client is still connected.

      if (!client->connected()) {
        //  delete this client reference if s/he has disconnected
        //  and don't put it back on the queue anymore. Bye!
        delete client;
      }
      else {

        //  Ok. This is an actively connected client.
        //  Let's grab a semaphore to prevent frame changes while we
        //  are serving this frame
        xSemaphoreTake( frameSync, portMAX_DELAY );

        client->write(CTNTTYPE, cntLen);
        sprintf(buf, "%d\r\n\r\n", camSize);
        client->write(buf, strlen(buf));
        client->write((char*) camBuf, (size_t)camSize);
        client->write(BOUNDARY, bdrLen);

        // Since this client is still connected, push it to the end
        // of the queue for further processing
        xQueueSend(streamingClients, (void *) &client, 0);

        //  The frame has been served. Release the semaphore and let other tasks run.
        //  If there is a frame switch ready, it will happen now in between frames
        xSemaphoreGive( frameSync );
        taskYIELD();
      }
    }
    else {
      //  Since there are no connected clients, there is no reason to waste battery running
      vTaskSuspend(NULL);
    }
    //  Let other tasks run after serving every client
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}


const char JHEADER[] = "HTTP/1.1 200 OK\r\n" \
                       "Content-disposition: inline; filename=capture.jpg\r\n" \
                       "Content-type: image/jpeg\r\n\r\n";
const int jhdLen = strlen(JHEADER);

// ==== Serve up one JPEG frame =============================================
void handleJPG(void)
{
  WiFiClient client = server.client();

  if (!client.connected()) return;
  cam.run();
  client.write(JHEADER, jhdLen);
  client.write((char*)cam.getfb(), cam.getSize());
}


// ==== Handle invalid URL requests ============================================
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
  server.send(200, "text / plain", message);
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