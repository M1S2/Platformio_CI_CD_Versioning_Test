#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include "updateHandling.h"
#include "wifiHandling.h"

UpdateChannel currentUpdateChannel = UPDATE_CHANNEL_DEV;

const char* stableBaseUrl = "https://github.com/M1S2/Platformio_CI_CD_Versioning_Test/releases/latest/download/";
const char* devBaseUrl = "https://M1S2.github.io/Platformio_CI_CD_Versioning_Test/firmware/dev/";
const char* manifestFilename = "manifest.json";

/*
Stable Manifest Format:
{
  "version": "3.0.0",
  "part1_fw": "part1_fw_3.0.0.bin",
  "part1_fs": "part1_fs_3.0.0.bin",
  "part1_fw_md5": "4375b4f6083364c9dcd557f80cadd149",
  "part1_fs_md5": "0b37d70272041137295d7dc4ca508698"
}

Dev Manifest Format:
{
  "version": "dev-SW_v3.0.0-p3-9853261",
  "part1_fw": "part1_fw.bin",
  "part1_fs": "part1_fs.bin",
  "part1_fw_md5": "4375b4f6083364c9dcd557f80cadd149",
  "part1_fs_md5": "0b37d70272041137295d7dc4ca508698"
}
*/

update_info_t updateInfo_Part1;
bool fetchNewestVersionInfos = false;
bool isUpdating = false;
bool currentlyUpdatingFs = false;

/**********************************************************************/

bool updateHandling_fetchVersion(update_info_t &info, String componentName)
{
    info.componentName = componentName;
    info.valid = false;
    const char* baseUrl = (currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? stableBaseUrl : devBaseUrl;
    String manifestUrl = String(baseUrl) + manifestFilename;

    #ifdef DEBUG_OUTPUT
        Serial.printf("Checking for %s update...\n", (currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev");
    #endif

    WiFiClientSecure client;
    client.setInsecure(); // TODO: remove this later...

    HTTPClient http;
    http.setTimeout(10000); // 10 Seconds
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, manifestUrl.c_str());
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

    String keyFw = componentName + "_fw";
    String keyFs = componentName + "_fs";
    String keyFwMd5 = componentName + "_fw_md5";
    String keyFsMd5 = componentName + "_fs_md5";

    info.version = doc["version"].as<String>();
    info.url_fw = String(baseUrl) + doc[keyFw].as<String>();
    info.url_fs = String(baseUrl) + doc[keyFs].as<String>();
    info.fw_md5 = doc[keyFwMd5].as<String>();
    info.fs_md5 = doc[keyFsMd5].as<String>();
    info.has_fs_update = (info.url_fs != "");
    info.valid = true;

    return info.valid;
}

/**********************************************************************/

bool updateHandling_performUpdate(update_info_t &info)
{
    if (!info.valid) return false;

    #ifdef DEBUG_OUTPUT
        Serial.printf("Performing update for component %s to version %s\n", info.componentName.c_str(), info.version.c_str());
    #endif

    WiFiClientSecure client;
    client.setInsecure(); // TODO: remove this later...

    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);

    currentlyUpdatingFs = false;

    ESPhttpUpdate.onProgress([&info](int cur, int total)
    {
        float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
        #ifdef DEBUG_OUTPUT
            Serial.printf("Progress: %d / %d (%.2f%%)\n", cur, total, percent);
        #endif
        if(!currentlyUpdatingFs)
        {
            info.updateProgress_fw = percent;
        }
        else
        {
            info.updateProgress_fs = percent;
        }
        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    bool fsUpdateResult = true;
    if(info.has_fs_update)
    {
        currentlyUpdatingFs = true;
        #ifdef DEBUG_OUTPUT
            Serial.println("Update file system...");
        #endif
        ESPhttpUpdate.setMD5sum(info.fs_md5);
        t_httpUpdate_return returnFsUpdate = ESPhttpUpdate.updateFS(client, info.url_fs);
        switch (returnFsUpdate)
        {
            case HTTP_UPDATE_FAILED:
                #ifdef DEBUG_OUTPUT
                    Serial.printf("FS Update failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
                #endif
                break;
            case HTTP_UPDATE_NO_UPDATES:
                #ifdef DEBUG_OUTPUT    
                    Serial.println("No FS update available");
                #endif
                break;
            case HTTP_UPDATE_OK:
                #ifdef DEBUG_OUTPUT    
                    Serial.println("FS Update ok");
                #endif
                break;
        }
        fsUpdateResult = (returnFsUpdate == HTTP_UPDATE_OK);
        currentlyUpdatingFs = false;
    }

    bool fwUpdateResult = true;
    #ifdef DEBUG_OUTPUT
        Serial.println("Update firmware...");
    #endif
    ESPhttpUpdate.setMD5sum(info.fw_md5);
    t_httpUpdate_return returnFwUpdate = ESPhttpUpdate.update(client, info.url_fw);
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

    return fsUpdateResult && fwUpdateResult;
}

/**********************************************************************/

void updateHandling_clearVersionInfo(update_info_t &info)
{
    info.valid = false;
    info.version = "";
    info.url_fw = "";
    info.url_fs = "";
    info.fw_md5 = "";
    info.fs_md5 = "";
    info.has_fs_update = false;
    info.updateProgress_fw = 0.0f;
    info.updateProgress_fs = 0.0f;
    isUpdating = false;
    fetchNewestVersionInfos = false;
}

/**********************************************************************/

void updateHandling_initWebserverEndpoints()
{
    server.on("/update/set_channel_dev", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        currentUpdateChannel = UPDATE_CHANNEL_DEV;
        updateHandling_clearVersionInfo(updateInfo_Part1);
        request->send(200, "text/plain", "Channel set to dev");
    });

    server.on("/update/set_channel_stable", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        currentUpdateChannel = UPDATE_CHANNEL_STABLE;
        updateHandling_clearVersionInfo(updateInfo_Part1);
        request->send(200, "text/plain", "Channel set to stable");
    });

    server.on("/update/start_fetch", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        updateHandling_startFetchingNewestVersionInfos();
        request->send(200, "text/plain", "Fetching...");
    });

    server.on("/update/start", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        updateHandling_startUpdate();
        request->send(200, "text/plain", "Update started");
    });

    server.on("/update/get_info", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        DynamicJsonDocument doc(2048);
        doc["channel"] = (currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev";
        doc["isFetching"] = fetchNewestVersionInfos;
        doc["isUpdating"] = isUpdating;
        doc["componentName"] = updateInfo_Part1.componentName;
        doc["available"] = updateInfo_Part1.valid;
        doc["has_fs_update"] = updateInfo_Part1.has_fs_update;
        doc["version"] = updateInfo_Part1.version;
        doc["url_fw"] = updateInfo_Part1.url_fw;
        doc["url_fs"] = updateInfo_Part1.url_fs;
        doc["fw_md5"] = updateInfo_Part1.fw_md5;
        doc["fs_md5"] = updateInfo_Part1.fs_md5;
        doc["updateProgress_fw"] = updateInfo_Part1.updateProgress_fw;
        doc["updateProgress_fs"] = updateInfo_Part1.updateProgress_fs;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

/**********************************************************************/

void updateHandling_startFetchingNewestVersionInfos()
{
    updateHandling_clearVersionInfo(updateInfo_Part1);

    // Set flag to fetch the newest version infos in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    fetchNewestVersionInfos = true;
}

void updateHandling_startUpdate()
{    
    // Set flag to perform the update in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    isUpdating = true;
}

/**********************************************************************/

void updateHandling_loop()
{
    if(fetchNewestVersionInfos)
    {
        String newVersion;
        updateHandling_fetchVersion(updateInfo_Part1, "part1");
        #ifdef DEBUG_OUTPUT
            Serial.print("New version for part1: ");
            Serial.print(updateInfo_Part1.valid ? "true" : "false");
            Serial.print(", version: ");
            Serial.print(updateInfo_Part1.version);
        #endif
        fetchNewestVersionInfos = false;
    }
    else if(isUpdating)
    {
        if(!updateInfo_Part1.valid)
        {
            // If no valid update info is available, fetch it first before trying to perform the update (e.g. in case the user directly clicks the "Start Update" button without first clicking the "Fetch Newest Version Infos" button in the web interface)
            fetchNewestVersionInfos = true;
        }
        else
        {
            updateHandling_performUpdate(updateInfo_Part1);
            isUpdating = false;
        }
    }
}