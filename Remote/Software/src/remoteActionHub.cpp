#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "remoteActionHub.h"
#include "updateHandling.h"
#include "config.h"

String remoteActionHub_findAP()
{
    #ifdef DEBUG_OUTPUT
        Serial.println("[Remote ActionHub] Scan for AP...");
    #endif

    int networkCount = WiFi.scanNetworks();
    if (networkCount <= 0)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Remote ActionHub] No WiFi networks found.");
        #endif
        return "";
    }

    String bestMatch = "";
    int bestRssi = -9999;
    for (int i = 0; i < networkCount; i++)
    {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);

        #ifdef DEBUG_OUTPUT
            uint8_t channel = WiFi.channel(i);
            Serial.print("[Remote ActionHub] Found: ");
            Serial.print(ssid);
            Serial.print(" | RSSI: ");
            Serial.print(rssi);
            Serial.print(" | Channel: ");
            Serial.println(channel);
        #endif

        if (ssid.startsWith(REMOTEACTIONHUB_AP_NAME_BASE))
        {
            if (rssi > bestRssi)
            {
                bestRssi = rssi;
                bestMatch = ssid;
            }
        }
    }

    #ifdef DEBUG_OUTPUT
        if (bestMatch.length() > 0)
        {
            Serial.print("[Remote ActionHub] AP choosen: ");
            Serial.println(bestMatch);
        }
        else
        {
            Serial.println("[Remote ActionHub] No matching AP found.");
        }
    #endif
    return bestMatch;
}

/**********************************************************************/

bool remoteActionHub_connectToAP(const String& ssid)
{
    #ifdef DEBUG_OUTPUT
        Serial.print("[Remote ActionHub] Connect with AP: ");
        Serial.println(ssid);
    #endif

    if (REMOTEACTIONHUB_AP_PW == nullptr || strlen(REMOTEACTIONHUB_AP_PW) == 0)
    {
        WiFi.begin(ssid.c_str());
    }
    else
    {
        WiFi.begin(ssid.c_str(), REMOTEACTIONHUB_AP_PW);
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < REMOTEACTIONHUB_CONNECT_TIMEOUT_MS)
    {
        delay(250);
        #ifdef DEBUG_OUTPUT
            Serial.print(".");
        #endif
    }

    #ifdef DEBUG_OUTPUT
        Serial.println();
    #endif

    if (WiFi.status() == WL_CONNECTED)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Remote ActionHub] Connected with AP.");
            Serial.print("[Remote ActionHub] My IP: ");
            Serial.println(WiFi.localIP());
            Serial.print("[Remote ActionHub] Gateway IP: ");
            Serial.println(WiFi.gatewayIP());
        #endif
        return true;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println("[Remote ActionHub] Paring with AP failed.");
    #endif
    return false;
}

/**********************************************************************/

bool remoteActionHub_getRequestedAction(RemoteActionHubAction& action)
{
    WiFiClient client;
    HTTPClient http;

    IPAddress gatewayIp = WiFi.gatewayIP();
    String actionUrl = "http://" + gatewayIp.toString() + REMOTEACTIONHUB_CURRENT_ACTION_ENDPOINT;

    #ifdef DEBUG_OUTPUT
        Serial.print("[Remote ActionHub] Gateway IP: ");
        Serial.println(gatewayIp.toString());
        Serial.print("[Remote ActionHub] Fetching action from: ");
        Serial.println(actionUrl);
    #endif

    if (!http.begin(client, actionUrl))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Remote ActionHub] HTTP begin() failed.");
        #endif
        return false;
    }

    // Follow redirects and set a reasonable timeout
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(5000); 

    int httpCode = http.GET();
    if (httpCode <= 0)
    {
        #ifdef DEBUG_OUTPUT
            Serial.print("[Remote ActionHub] HTTP Error: ");
            Serial.println(http.errorToString(httpCode));
        #endif
        http.end();
        return false;
    }

    #ifdef DEBUG_OUTPUT 
        Serial.print("[Remote ActionHub] HTTP Status: ");
        Serial.println(httpCode);
    #endif
    if (httpCode != HTTP_CODE_OK)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Remote ActionHub] Unexpected HTTP Status: %d\n", httpCode);
        #endif
        http.end();
        return false;
    }

    String payload = http.getString();
    payload.trim();
    int receivedVal = payload.toInt();  // toInt() returns 0, if failed. This will lead to action NONE (this is ok here).    
    if (receivedVal < REMOTEACTIONHUB_ACTION_NONE || receivedVal > REMOTEACTIONHUB_ACTION_UPDATE)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Remote ActionHub] Action out of range: %d\n", receivedVal);
        #endif
        action = REMOTEACTIONHUB_ACTION_NONE;
    }
    else
    {
        action = static_cast<RemoteActionHubAction>(receivedVal);
    }

    #ifdef DEBUG_OUTPUT
        Serial.print("[Remote ActionHub] Action received: ");
        Serial.println((int)action);
    #endif

    http.end();
    return true;
}

/**********************************************************************/

bool remoteActionHub_runAction()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    String actionHubSsid = remoteActionHub_findAP();
    if (actionHubSsid.length() == 0)
    {
        return false;
    }
    if (!remoteActionHub_connectToAP(actionHubSsid))
    {
        return false;
    }
    
    RemoteActionHubAction requestedAction = REMOTEACTIONHUB_ACTION_NONE;
    if (!remoteActionHub_getRequestedAction(requestedAction))
    {
        return false;
    }

    bool result = true;
    switch (requestedAction)
    {
        case REMOTEACTIONHUB_ACTION_UPDATE:
            result = updateHandling_performUpdate();
            break;
        default: break;
    }
    return result;
}