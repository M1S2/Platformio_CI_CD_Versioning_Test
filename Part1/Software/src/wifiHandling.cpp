#include <Schedule.h>
#include <ESPAsyncWiFiManager.h>
#include "wifiHandling.h"
#include "otaUpdate.h"
#include "config.h"
#include "main.h"

AsyncWebServer server(80);
DNSServer dns;
AsyncWiFiManager wifiManager(&server, &dns);

bool wifiConfig_isAPOpen;

// The event handlers are initialized in the setup()
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

void wifiHandling_eraseCredentials() 
{
    WiFi.disconnect(true);
    ESP.eraseConfig();
    #ifdef DEBUG_OUTPUT
        Serial.println("WiFi credentials erased");
    #endif
}

void wifiHandling_initWebserverFiles()
{
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.onNotFound([](AsyncWebServerRequest *request)
    {
        request->send(404, "text/plain", "Not found");
    });
}

void wifiHandling_wifiManagerSaveCB()
{
    wifiConfig_isAPOpen = false;
    WiFi.softAPdisconnect(true);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);            // IMPORTANT: set this to STA only (no AP, Encryption seems not to work with WIFI_AP_STA mode)
    WiFi.begin();

    #ifdef DEBUG_OUTPUT
        Serial.print("Station IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Wi-Fi Channel: ");
        Serial.println(WiFi.channel());
    #endif

    // https://randomnerdtutorials.com/solved-reconnect-esp8266-nodemcu-to-wifi/
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    wifiHandling_initWebserverFiles();
    main_initWebserverEndpoints();
    otaUpdate_init(&server);

    // start webserver
    server.begin();
}

void wifiHandling_wifiManagerAPOpenedCB(AsyncWiFiManager* manager)
{
    wifiConfig_isAPOpen = true;
    WiFi.persistent(true);
    manager->setConnectTimeout(CONNECTION_TIMEOUT_MS / 1000);
}

void wifiHandling_wifiManagerLoop()
{
    wifiManager.loop();
}

void wifiHandling_init()
{
    #ifdef DEBUG_OUTPUT
        wifiManager.setDebugOutput(true);
    #else
        wifiManager.setDebugOutput(false);
    #endif

    wifiManager.setSaveConfigCallback(wifiHandling_wifiManagerSaveCB);
    wifiManager.setAPCallback(wifiHandling_wifiManagerAPOpenedCB);

    WiFi.hostname(WIFI_HOSTNAME);

    WiFi.mode(WIFI_AP_STA);         // Set the device as a Station and Soft Access Point simultaneously
    WiFi.begin();
    boolean keepConnecting = true;
    uint8_t connectionStatus;
    unsigned long start = millis();
    while (keepConnecting)
    {
        connectionStatus = WiFi.status();
        if (millis() > start + CONNECTION_TIMEOUT_MS)
        {
            keepConnecting = false;
            #ifdef DEBUG_OUTPUT
                Serial.println("Connection timed out");
            #endif
        }
        if (connectionStatus == WL_CONNECTED || connectionStatus == WL_CONNECT_FAILED)
        {
            keepConnecting = false;
        }
    }
    if(connectionStatus != WL_CONNECTED)
    {
        wifiManager.setConnectTimeout(1);
        wifiManager.startConfigPortalModeless(CONFIGURATION_AP_NAME, CONFIGURATION_AP_PW);
    }
    else
    {
        wifiHandling_wifiManagerSaveCB();
    }
}