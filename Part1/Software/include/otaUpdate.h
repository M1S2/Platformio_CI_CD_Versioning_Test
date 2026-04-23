#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

void otaUpdate_init(AsyncWebServer* server);
void otaUpdate_loop();

#endif