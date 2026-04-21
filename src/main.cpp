#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "main.h"
#include "config.h"
#include "wifiHandling.h"
#include "timeHandling.h"
#include "otaUpdate.h"
#include "version.h"

void main_initWebserverEndpoints()
{
    server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        DynamicJsonDocument doc(256);
        doc["text"] = "Hello World";
        doc["version"] = FW_VERSION;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

/**********************************************************************/

void setup()
{
    #ifdef DEBUG_OUTPUT
        Serial.begin(115200);
    #endif
    pinMode(LED_BUILTIN, OUTPUT);

    #ifdef DEBUG_OUTPUT
        Serial.print("FW Version: ");
        Serial.println(FW_VERSION);
    #endif

    // Begin LittleFS
    if (!LittleFS.begin())
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("An Error has occurred while mounting LittleFS");
        #endif
        return;
    }

    timeHandling_init();
    wifiHandling_init();

    digitalWrite(LED_BUILTIN, LOW);            // Turn on LED
}

/**********************************************************************/

void loop()
{
    wifiHandling_wifiManagerLoop();
    otaUpdate_loop();
}
