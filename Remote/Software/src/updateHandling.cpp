#include <ESP8266httpUpdate.h>
#include "updateHandling.h"
#include "config.h"

bool updateHandling_getMd5Hash(String& md5Hash)
{
    WiFiClient client;
    HTTPClient http;

    IPAddress gatewayIp = WiFi.gatewayIP();
    String md5Url = "http://" + gatewayIp.toString() + UPDATE_MD5_ENDPOINT;

    #ifdef DEBUG_OUTPUT
        Serial.print("[Update Handling] Gateway IP: ");
        Serial.println(gatewayIp.toString());
        Serial.print("[Update Handling] Fetching MD5 from: ");
        Serial.println(md5Url);
    #endif

    if (!http.begin(client, md5Url))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling] HTTP begin() failed.");
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
            Serial.print("[Update Handling] HTTP Error: ");
            Serial.println(http.errorToString(httpCode));
        #endif
        http.end();
        return false;
    }

    #ifdef DEBUG_OUTPUT 
        Serial.print("[Update Handling] HTTP Status: ");
        Serial.println(httpCode);
    #endif
    if (httpCode != HTTP_CODE_OK)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling] Unexpected HTTP Status: %d\n", httpCode);
        #endif
        http.end();
        return false;
    }

    String payload = http.getString();
    payload.trim();

    if (payload.length() != 32)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling] Invalid MD5 length: %d (expected 32)\n", payload.length());
        #endif
        http.end();
        return false;
    }

    md5Hash = payload;
    http.end();
    return true;
}

void updateHandling_reportProgress(float progress, bool finished = false)
{
    WiFiClient client;
    HTTPClient http;

    IPAddress gatewayIp = WiFi.gatewayIP();
    String reportProgressUrl = "http://" + gatewayIp.toString() + UPDATE_REPORT_PROGRESS_ENDPOINT;

    String postData = "progress=" + String(progress, 2) + "&finished=" + (finished ? "true" : "false");
    #ifdef DEBUG_OUTPUT
        Serial.print("[Update Handling] Reporting progress to: ");
        Serial.print(reportProgressUrl);
        Serial.print(" with data: ");
        Serial.println(postData);
    #endif

    if (!http.begin(client, reportProgressUrl))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling] HTTP begin() for progress report failed.");
        #endif
        return;
    }

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(postData);

    #ifdef DEBUG_OUTPUT
        if (httpCode <= 0)
        {
            Serial.printf("[Update Handling] Progress report HTTP Error: %d -> %s\n", httpCode, http.errorToString(httpCode).c_str());
        }
    #endif
    http.end();
}

bool updateHandling_performUpdate()
{
    String fwMd5;
    if(!updateHandling_getMd5Hash(fwMd5))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling] Failed to get MD5 hash.");
        #endif
        return false;
    }

    WiFiClient client;
    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);
    ESPhttpUpdate.rebootOnUpdate(false);    // Don't reboot automatically after the firmware update.

    IPAddress gatewayIp = WiFi.gatewayIP();
    String fwUrl = "http://" + gatewayIp.toString() + UPDATE_BIN_ENDPOINT;
    #ifdef DEBUG_OUTPUT
        Serial.print("[Update Handling] Firmware URL: ");
        Serial.println(fwUrl);
    #endif

    static unsigned long lastProgressEventTime = 0;

    ESPhttpUpdate.onStart([]()
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling] Update started.");
        #endif
        updateHandling_reportProgress(0.0f, false);
    });
    ESPhttpUpdate.onEnd([]()
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling] Update ended.");
        #endif
        updateHandling_reportProgress(100.0f, true);
    });
    ESPhttpUpdate.onProgress([](int cur, int total)
    {        
        float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling] Progress: %d / %d (%.2f%%)\n", cur, total, percent);
        #endif
                       
        // Send event only every UPDATE_PROGRESS_INTERVALL_DURING_UPDATE_MS
        unsigned long currentTime = millis();
        if (currentTime - lastProgressEventTime >= UPDATE_PROGRESS_INTERVALL_DURING_UPDATE_MS)
        {
            updateHandling_reportProgress(percent, false);
            lastProgressEventTime = currentTime;
        }

        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    bool fwUpdateResult = true;
    #ifdef DEBUG_OUTPUT
        Serial.println("Update firmware...");
    #endif
    ESPhttpUpdate.setMD5sum(fwMd5.c_str());

    t_httpUpdate_return returnFwUpdate = ESPhttpUpdate.update(client, fwUrl);
    switch (returnFwUpdate)
    {
        case HTTP_UPDATE_FAILED:
            #ifdef DEBUG_OUTPUT
                Serial.printf("Update failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
            #endif
            break;
        case HTTP_UPDATE_NO_UPDATES:
            #ifdef DEBUG_OUTPUT
                Serial.println("No update available");
            #endif
            break;
        case HTTP_UPDATE_OK:
            #ifdef DEBUG_OUTPUT
                Serial.println("Update ok");
            #endif
            break;
    }
    fwUpdateResult = (returnFwUpdate == HTTP_UPDATE_OK);

    if(fwUpdateResult)
    {
        // Wait for some seconds to ensure that the HTTP response is sent completely before restarting.
        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            yield();
        }
        ESP.restart();
    }
    else
    {
        /* Failed to update. No need to restart */
    }

    return fwUpdateResult;
}