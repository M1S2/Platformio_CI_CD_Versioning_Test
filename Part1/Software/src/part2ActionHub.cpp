#include <ESP8266WiFi.h>
#include "part2ActionHub.h"
#include "config.h"

bool part2ActionHub_isAPOpen = false;
String part2ActionHub_ApSsid;

unsigned long part2ActionHub_APStartedAt = 0;

bool part2ActionHub_startAP()
{
    if (part2ActionHub_isAPOpen)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[ActionHub AP] AP is already active.");
        #endif
        return true;
    }

    part2ActionHub_ApSsid = PART2ACTIONHUB_AP_NAME_BASE + String(ESP.getChipId(), HEX);

    #ifdef DEBUG_OUTPUT
        Serial.println();
        Serial.println("[ActionHub AP] Start Access Point...");
    #endif

    // Make sure, AP is supported. Caution: No encryption is possible in WIFI_AP_STA mode, so reset this mode to WIFI_STA after closing the AP.
    WiFi.mode(WIFI_AP_STA);

    bool apStarted = false;
    if (PART2ACTIONHUB_AP_PW == nullptr || strlen(PART2ACTIONHUB_AP_PW) == 0)
    {
        apStarted = WiFi.softAP(part2ActionHub_ApSsid.c_str());
    }
    else
    {
        apStarted = WiFi.softAP(part2ActionHub_ApSsid.c_str(), PART2ACTIONHUB_AP_PW);
    }

    if (!apStarted)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[ActionHub AP] ERROR: SoftAP couldn't be started.");
        #endif
        return false;
    }

    part2ActionHub_isAPOpen = true;
    part2ActionHub_APStartedAt = millis();

    #ifdef DEBUG_OUTPUT
        delay(100);    // delay a bit to ensure that the AP is fully started before printing the info
        Serial.println("[ActionHub AP] SoftAP successfully started.");
        Serial.print("[ActionHub AP] SSID: ");
        Serial.println(part2ActionHub_ApSsid);

        Serial.print("[ActionHub AP] AP IP: ");
        Serial.println(WiFi.softAPIP());

        Serial.print("[ActionHub AP] AP MAC: ");
        Serial.println(WiFi.softAPmacAddress());

        Serial.print("[ActionHub AP] STA IP: ");
        Serial.println(WiFi.localIP());

        Serial.print("[ActionHub AP] STA connected: ");
        Serial.println(WiFi.status() == WL_CONNECTED ? "YES" : "NO");

        Serial.print("[ActionHub AP] WiFi Channel: ");
        Serial.println(WiFi.channel());
    #endif

    return true;
}

/**********************************************************************/

bool part2ActionHub_stopAP()
{
    if (!part2ActionHub_isAPOpen)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[ActionHub AP] AP isn't active.");
        #endif
        return true;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println();
        Serial.println("[ActionHub AP] Stop Access Point...");
    #endif

    bool result = WiFi.softAPdisconnect(true); // true = AP off + disconnect clients
    if (!result)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[ActionHub AP] WARNING: softAPdisconnect() returns false.");
        #endif
    }

    // Make sure, AP is disabled. Caution: Encryption is only possible in WIFI_STA mode.
    WiFi.mode(WIFI_STA);

    part2ActionHub_isAPOpen = false;
    part2ActionHub_APStartedAt = 0;

    #ifdef DEBUG_OUTPUT
        Serial.println("[ActionHub AP] SoftAP stopped.");
    #endif

    return true;
}

/**********************************************************************/

bool part2ActionHub_handleAPTimeout()
{
    if (part2ActionHub_isAPOpen && PART2ACTIONHUB_AP_TIMEOUT_MS > 0 && (millis() - part2ActionHub_APStartedAt >= PART2ACTIONHUB_AP_TIMEOUT_MS))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[ActionHub AP] Timeout reached, AP is automatically stopped.");
        #endif
        part2ActionHub_stopAP();
        return true;    // return true if AP was stopped due to timeout
    }
    return false;
}