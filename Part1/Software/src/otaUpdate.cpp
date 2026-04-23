#include "otaUpdate.h"

unsigned long ota_progress_millis = 0;

void onOTAStart() 
{
    #ifdef DEBUG_OUTPUT
        Serial.println("OTA update started!");
    #endif
}

void onOTAProgress(size_t current, size_t final)
{
    // Log every 1 second
    if (millis() - ota_progress_millis > 1000)
    {
        ota_progress_millis = millis();
        #ifdef DEBUG_OUTPUT
            Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
        #endif
    }
}

void onOTAEnd(bool success)
{
    if (success)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("OTA update finished successfully!");
        #endif
    }
    else 
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("There was an error during OTA update!");
        #endif
    }
}

void otaUpdate_init(AsyncWebServer* server)
{
    ElegantOTA.begin(server);
    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onProgress(onOTAProgress);
    ElegantOTA.onEnd(onOTAEnd);
}

void otaUpdate_loop()
{
    ElegantOTA.loop();
}