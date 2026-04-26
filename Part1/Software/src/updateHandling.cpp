#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "updateHandling.h"
#include "wifiHandling.h"

UpdateChannel currentUpdateChannel = UPDATE_CHANNEL_DEV;

const char* manifestStable = "https://api.github.com/repos/M1S2/Platformio_CI_CD_Versioning_Test/releases/latest";
const char* devBaseUrl = "https://M1S2.github.io/Platformio_CI_CD_Versioning_Test/firmware/dev/";
const char* manifestDev = "https://M1S2.github.io/Platformio_CI_CD_Versioning_Test/firmware/dev/manifest.json";

update_info_t updateInfo;
bool fetchNewestVersionInfos = false;

/**********************************************************************/

bool updateHandling_fetchStableVersion(update_info_t &info)
{
    WiFiClientSecure client;
    client.setInsecure(); // TODO: remove this later...

    HTTPClient http;
    http.setTimeout(10000); // 10 Seconds
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, manifestStable);
    http.addHeader("User-Agent", "ESP8266");
    http.addHeader("Accept", "application/vnd.github+json");    

    int httpCode = http.GET();
    #ifdef DEBUG_OUTPUT
        Serial.printf("HTTP Code: %d\n", httpCode);
        if (httpCode <= 0)
        {
            Serial.printf("HTTP error: %s\n", http.errorToString(httpCode).c_str());
        }
    #endif

    if (httpCode != 200)
    {
        http.end();
        return false;
    }

    /*String payload = http.getString();
    #ifdef DEBUG_OUTPUT
        Serial.println("Received manifest:");
        Serial.println(payload);
    #endif
    http.end();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    */

    DynamicJsonDocument doc(32768);
    DeserializationError err = deserializeJson(doc, http.getStream());
    #ifdef DEBUG_OUTPUT
        Serial.print("Deserialization error: ");
        Serial.println(err.c_str());
    #endif
    http.end();
    
    String tag = doc["tag_name"].as<String>(); // SW_v1.2.3
    info.version = tag;

    info.url_fw = "";
    info.url_fs = "";
    // Search binary
    for (JsonObject asset : doc["assets"].as<JsonArray>())
    {
        String name = asset["name"].as<String>();
        if (name.indexOf("part1_fw") >= 0)
        {
            info.url_fw = asset["browser_download_url"].as<String>();
            info.valid = true;
        }
        else if (name.indexOf("part1_fs") >= 0)
        {
            info.url_fs = asset["browser_download_url"].as<String>();
            // valid isn't set here because the filesystem update is optional, so the version info is also valid if only the firmware update is available
        }
    }

    return info.valid;
}

bool updateHandling_fetchDevVersion(update_info_t &info)
{
    WiFiClientSecure client;
    client.setInsecure(); // TODO: remove this later...

    HTTPClient http;
    http.setTimeout(10000); // 10 Seconds
    http.begin(client, manifestDev);
    int httpCode = http.GET();
    #ifdef DEBUG_OUTPUT
        Serial.printf("HTTP Code: %d\n", httpCode);
        if (httpCode <= 0)
        {
            Serial.printf("HTTP error: %s\n", http.errorToString(httpCode).c_str());
        }
    #endif

    if (httpCode != 200)
    {
        http.end();
        return false;
    }

    String payload = http.getString();
    #ifdef DEBUG_OUTPUT
        Serial.println("Received manifest:");
        Serial.println(payload);
    #endif
    http.end();

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        return false;
    }

    info.version = doc["version"].as<String>();
    info.url_fw = String(devBaseUrl) + doc["part1_fw"].as<String>();
    info.url_fs = String(devBaseUrl) + doc["part1_fs"].as<String>();
    info.valid = true;

    return info.valid;
}

bool updateHandling_fetchVersion(update_info_t &info)
{
    if (currentUpdateChannel == UPDATE_CHANNEL_STABLE)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("Checking for stable update...");
        #endif
        return updateHandling_fetchStableVersion(info);
    }
    else
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("Checking for dev update...");
        #endif
        return updateHandling_fetchDevVersion(info);
    }
}

/**********************************************************************/

void updateHandling_clearVersionInfo()
{
    updateInfo.valid = false;
    updateInfo.version = "";
    updateInfo.url_fw = "";
    updateInfo.url_fs = "";
}

/**********************************************************************/

void updateHandling_initWebserverEndpoints()
{
    server.on("/update/set_channel_dev", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        currentUpdateChannel = UPDATE_CHANNEL_DEV;
        updateHandling_clearVersionInfo();
        request->send(200, "text/plain", "Channel set to dev");
    });

    server.on("/update/set_channel_stable", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        currentUpdateChannel = UPDATE_CHANNEL_STABLE;
        updateHandling_clearVersionInfo();
        request->send(200, "text/plain", "Channel set to stable");
    });

    server.on("/update/fetch", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        updateHandling_startFetchingNewestVersionInfos();
        request->send(200, "text/plain", "Fetching...");
    });

    server.on("/update/get_info", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        DynamicJsonDocument doc(1024);
        doc["channel"] = (currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev";
        doc["isFetching"] = fetchNewestVersionInfos;
        doc["available"] = updateInfo.valid;
        doc["version"] = updateInfo.version;
        doc["url_fw"] = updateInfo.url_fw;
        doc["url_fs"] = updateInfo.url_fs;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

/**********************************************************************/

void updateHandling_startFetchingNewestVersionInfos()
{
    updateHandling_clearVersionInfo();

    // Set flag to fetch the newest version infos in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    fetchNewestVersionInfos = true;
}

/**********************************************************************/

void updateHandling_loop()
{
    if(fetchNewestVersionInfos)
    {
        String newVersion;
        updateHandling_fetchVersion(updateInfo);
        #ifdef DEBUG_OUTPUT
            Serial.print("New version available: ");
            Serial.print(updateInfo.valid ? "true" : "false");
            Serial.print(", version: ");
            Serial.print(updateInfo.version);
            Serial.print(", URL_FW: ");
            Serial.print(updateInfo.url_fw);
            Serial.print(", URL_FS: ");
            Serial.println(updateInfo.url_fs);
        #endif
        fetchNewestVersionInfos = false;
    }
}

/**********************************************************************/

/*
bool otaPerformUpdate()
{
    if (firmwareUrl == "") return false;

    WiFiClient client;

    t_httpUpdate_return ret = ESPhttpUpdate.update(client, firmwareUrl);

    switch (ret)
    {
        case HTTP_UPDATE_FAILED:
            Serial.printf("Update failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("No update available");
            return false;

        case HTTP_UPDATE_OK:
            Serial.println("Update ok");
            return true;
    }

    return false;
}
*/