#include <ESP8266WiFi.h>
#include "remoteActionHub.h"
#include "wifiHandling.h"
#include "config.h"

bool remoteActionHub_isAPOpen = false;
String remoteActionHub_ApSsid;
RemoteActionHubAction remoteActionHub_currentAction = REMOTEACTIONHUB_ACTION_NONE;

unsigned long remoteActionHub_APStartedAt = 0;


void remoteActionHub_initWebserverEndpoints()
{
    server.on("/actionHub/current_action", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if(!remoteActionHub_isAPOpen)
        {
            request->send(404, "text/plain", "Action hub not open");
            return;
        }
        request->send(200, "text/plain", String(remoteActionHub_currentAction).c_str());
    });
}

/**********************************************************************/

bool remoteActionHub_startAP(RemoteActionHubAction currentAction)
{
    remoteActionHub_currentAction = currentAction;

    if (remoteActionHub_isAPOpen)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[ActionHub AP] AP is already active."));
        #endif
        return true;
    }

    remoteActionHub_ApSsid = REMOTEACTIONHUB_AP_NAME_BASE + String(ESP.getChipId(), HEX);

    #ifdef DEBUG_OUTPUT
        Serial.println();
        Serial.println(F("[ActionHub AP] Start Access Point..."));
    #endif

    // Make sure, AP is supported. Caution: No encryption is possible in WIFI_AP_STA mode, so reset this mode to WIFI_STA after closing the AP.
    WiFi.mode(WIFI_AP_STA);

    bool apStarted = false;
    if (REMOTEACTIONHUB_AP_PW == nullptr || strlen(REMOTEACTIONHUB_AP_PW) == 0)
    {
        apStarted = WiFi.softAP(remoteActionHub_ApSsid.c_str());
    }
    else
    {
        apStarted = WiFi.softAP(remoteActionHub_ApSsid.c_str(), REMOTEACTIONHUB_AP_PW);
    }

    if (!apStarted)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[ActionHub AP] ERROR: SoftAP couldn't be started."));
        #endif
        return false;
    }

    remoteActionHub_isAPOpen = true;
    remoteActionHub_APStartedAt = millis();

    #ifdef DEBUG_OUTPUT
        delay(100);    // delay a bit to ensure that the AP is fully started before printing the info
        Serial.println(F("[ActionHub AP] SoftAP successfully started."));
        Serial.print(F("[ActionHub AP] SSID: "));
        Serial.println(remoteActionHub_ApSsid);

        Serial.print(F("[ActionHub AP] AP IP: "));
        Serial.println(WiFi.softAPIP());

        Serial.print(F("[ActionHub AP] AP MAC: "));
        Serial.println(WiFi.softAPmacAddress());

        Serial.print(F("[ActionHub AP] STA IP: "));
        Serial.println(WiFi.localIP());

        Serial.print(F("[ActionHub AP] STA connected: "));
        Serial.println(WiFi.status() == WL_CONNECTED ? "YES" : "NO");

        Serial.print(F("[ActionHub AP] WiFi Channel: "));
        Serial.println(WiFi.channel());
    #endif

    return true;
}

/**********************************************************************/

bool remoteActionHub_stopAP()
{
    remoteActionHub_currentAction = REMOTEACTIONHUB_ACTION_NONE;

    if (!remoteActionHub_isAPOpen)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[ActionHub AP] AP isn't active."));
        #endif
        return true;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println();
        Serial.println(F("[ActionHub AP] Stop Access Point..."));
    #endif

    bool result = WiFi.softAPdisconnect(true); // true = AP off + disconnect clients
    if (!result)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[ActionHub AP] WARNING: softAPdisconnect() returns false."));
        #endif
    }

    // Make sure, AP is disabled. Caution: Encryption is only possible in WIFI_STA mode.
    WiFi.mode(WIFI_STA);

    remoteActionHub_isAPOpen = false;
    remoteActionHub_APStartedAt = 0;

    #ifdef DEBUG_OUTPUT
        Serial.println(F("[ActionHub AP] SoftAP stopped."));
    #endif

    return true;
}

/**********************************************************************/

bool remoteActionHub_handleAPTimeout()
{
    if (remoteActionHub_isAPOpen && REMOTEACTIONHUB_AP_TIMEOUT_MS > 0 && (millis() - remoteActionHub_APStartedAt >= REMOTEACTIONHUB_AP_TIMEOUT_MS))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[ActionHub AP] Timeout reached, AP is automatically stopped."));
        #endif
        remoteActionHub_stopAP();
        return true;    // return true if AP was stopped due to timeout
    }
    return false;
}