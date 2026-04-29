#include <Schedule.h>
#include <ESPAsyncWiFiManager.h>
#include <LittleFS.h>
#include "wifiHandling.h"
#include "config.h"
#include "main.h"
#include "updateHandling.h"

AsyncWebServer server(80);
DNSServer dns;
AsyncWiFiManager wifiManager(&server, &dns);

bool wifiConfig_isAPOpen;
WifiState wifiHandling_wifiState = WIFI_DISCONNECTED;
unsigned long wifiStartTime = 0;

// The event handlers are initialized in the init()
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

void wifiHandling_eraseCredentials() 
{
    WiFi.disconnect(true);
    ESP.eraseConfig();
    #ifdef DEBUG_OUTPUT
        Serial.println("WiFi credentials erased");
    #endif
    delay(500);
    ESP.restart();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) 
{
  // https://github.com/esp8266/Arduino/issues/5722
  // The wifi callbacks execute in the SYS context, and you can't yield/delay in there.
  // If you need to do that, I suggest to use the wifi callback to schedule another callback with your code that requires yield/delay.
  // Scheduled functions execute as though they were called from the loop, I.e. in CONT context.
  // Same applies to the onWifiDisconnect event
  
  //schedule_function(leds_wifiConnected);
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) 
{
  // This function gets called cyclic as long as the Wifi is disconnected
  if(!wifiConfig_isAPOpen)
  {
    //schedule_function(leds_wifiFailed);
  }
}

void wifiHandling_initWebserverFiles()
{
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.onNotFound([](AsyncWebServerRequest *request)
    {
        request->send(404, "text/plain", "Not found");
    });
}

void wifiHandling_onConnected()
{
    #ifdef DEBUG_OUTPUT
        Serial.println("WiFi connected");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    #endif

    wifiConfig_isAPOpen = false;

    wifiHandling_initWebserverFiles();
    main_initWebserverEndpoints();
    updateHandling_initWebserverEndpoints();

    server.begin();

    WiFi.setAutoReconnect(true);
}

void wifiHandling_wifiManagerSaveCB()
{
    #ifdef DEBUG_OUTPUT
        Serial.println("WiFi credentials saved");
    #endif
}

void wifiHandling_wifiManagerAPOpenedCB(AsyncWiFiManager* manager)
{
    wifiConfig_isAPOpen = true;
    manager->setConnectTimeout(CONFIGURATION_PORTAL_TIMEOUT_MS / 1000);
}

void wifiHandling_init()
{
    // https://randomnerdtutorials.com/solved-reconnect-esp8266-nodemcu-to-wifi/ 
    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-examples.html
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    WiFi.hostname(WIFI_HOSTNAME);

    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    delay(100);

    #ifdef DEBUG_OUTPUT
        wifiManager.setDebugOutput(true);
    #else
        wifiManager.setDebugOutput(false);
    #endif

    wifiManager.setSaveConfigCallback(wifiHandling_wifiManagerSaveCB);
    wifiManager.setAPCallback(wifiHandling_wifiManagerAPOpenedCB);

    wifiStartTime = millis();

    if (WiFi.SSID().length() > 0)
    {
        WiFi.begin();
        wifiHandling_wifiState = WIFI_CONNECTING;
    }
    else
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("No stored credentials");
        #endif
        wifiHandling_wifiState = WIFI_START_PORTAL;
    }
}

void wifiHandling_loop()
{
    // e.g. leds.service();

    switch (wifiHandling_wifiState)
    {
        case WIFI_DISCONNECTED:
        {
            // nothing to do here, connect is triggered in the wifiHandling_init() function. Just wait for the state to change to CONNECTING, which is done in the init function
            break;
        }
        case WIFI_CONNECTING:
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                #ifdef DEBUG_OUTPUT
                    Serial.println("WIFI_CONNECTING -> WIFI_CONNECTED");
                #endif

                wifiHandling_onConnected();
                wifiHandling_wifiState = WIFI_CONNECTED;
                break;
            }

            // kurzer Timeout
            if (millis() - wifiStartTime > CONNECTION_TIMEOUT_MS)
            {
                #ifdef DEBUG_OUTPUT
                    Serial.println("Initial connect failed → starting portal");
                #endif
                wifiHandling_wifiState = WIFI_START_PORTAL;
            }
            break;
        }
        case WIFI_START_PORTAL:
        {
            WiFi.disconnect();
            delay(100);
            wifiManager.setConnectTimeout(1);
            wifiManager.startConfigPortalModeless(CONFIGURATION_AP_NAME, CONFIGURATION_AP_PW);
            wifiHandling_wifiState = WIFI_PORTAL;
            break;
        }
        case WIFI_PORTAL:
        {
            wifiManager.loop();
            if (WiFi.status() == WL_CONNECTED)
            {
                #ifdef DEBUG_OUTPUT
                    Serial.println("WIFI_PORTAL -> WIFI_CONNECTED");
                #endif

                WiFi.softAPdisconnect(true);    // close the AP
                WiFi.mode(WIFI_STA);            // back to pure station mode, otherwise the AP is still open and the encryption doesn't work (at least with WIFI_AP_STA mode

                wifiHandling_onConnected();
                wifiHandling_wifiState = WIFI_CONNECTED;
            }
            break;
        }
        case WIFI_CONNECTED:
        {
            // nothing to do here, everything is handled by the event handlers
            break;
        }
    }
}