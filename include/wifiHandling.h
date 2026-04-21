#ifndef WIFI_HANDLING_H
#define WIFI_HANDLING_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
extern AsyncEventSource events;

#define SERVER_EVENT_SOURCE                     "/events"
#define SERVER_EVENT_SENSOR_PAIRED              "sensorPaired"
#define SERVER_EVENT_SENSOR_PAIRING_TIMEOUT     "sensorPairingTimeout"
#define SERVER_EVENT_SENSOR_NEW_MESSAGE         "newSensorMessage"
#define SERVER_EVENT_SENSOR_MODE_CHANGED        "sensorModeChanged"

extern bool wifiConfig_isAPOpen;

void wifiHandling_wifiManagerLoop();
void wifiHandling_init();
void wifiHandling_eraseCredentials();

#endif