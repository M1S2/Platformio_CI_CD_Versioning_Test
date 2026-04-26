#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "main.h"
#include "config.h"
#include "wifiHandling.h"
#include "otaUpdate.h"
#include "version.h"

void main_initWebserverEndpoints()
{
    server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        DynamicJsonDocument doc(256);
        doc["text"] = "Hello World Text";
        doc["version"] = FW_VERSION;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/eraseCredentials", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // Send back the response before erasing the credentials, otherwise the client would not receive the response because the device restarts immediately after erasing the credentials
        request->send(200, "text/plain", "Credentials erased"); 
        wifiHandling_eraseCredentials();
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

    wifiHandling_init();

    digitalWrite(LED_BUILTIN, LOW);            // Turn on LED
}

/**********************************************************************/

void loop()
{
    wifiHandling_loop();
    //otaUpdate_loop();
}
