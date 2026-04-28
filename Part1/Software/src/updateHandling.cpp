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
  "part1_fw_sha256": "part1_fw_3.0.0.sha256",
  "part1_fs_sha256": "part1_fs_3.0.0.sha256"
}

Dev Manifest Format:
{
  "version": "dev-SW_v2.0.0-p2-856538c",
  "part1_fw": "part1_fw.bin",
  "part1_fs": "part1_fs.bin",
  "part1_fw_sha256": "part1_fw.sha256",
  "part1_fs_sha256": "part1_fs.sha256"
}
*/

update_info_t updateInfo;
bool fetchNewestVersionInfos = false;
bool isUpdating = false;
bool currentlyUpdatingFs = false;

/**********************************************************************/

bool updateHandling_fetchVersion(update_info_t &info)
{
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

    info.version = doc["version"].as<String>();
    info.url_fw = String(baseUrl) + doc["part1_fw"].as<String>();
    info.url_fs = String(baseUrl) + doc["part1_fs"].as<String>();
    info.url_fw_sha256 = String(baseUrl) + doc["part1_fw_sha256"].as<String>();
    info.url_fs_sha256 = String(baseUrl) + doc["part1_fs_sha256"].as<String>();
    info.has_fs_update = (info.url_fs != "");
    info.valid = true;

    return info.valid;
}

/**********************************************************************/

#warning TODO: check the SHA256 hash of the downloaded firmware and filesystem binaries before applying the update, to prevent bricking the device by applying a corrupted update (e.g. due to network issues during download)

bool updateHandling_performUpdate()
{
    if (!updateInfo.valid) return false;

    #ifdef DEBUG_OUTPUT
        Serial.printf("Performing update to version %s\n", updateInfo.version.c_str());
    #endif

    WiFiClientSecure client;
    client.setInsecure(); // TODO: remove this later...

    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);

    currentlyUpdatingFs = false;

    #ifdef DEBUG_OUTPUT
        ESPhttpUpdate.onProgress([](int cur, int total)
        {
            float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
            Serial.printf("Progress: %d / %d (%.2f%%)\n", cur, total, percent);
            if(!currentlyUpdatingFs)
            {
                updateInfo.updateProgress_fw = percent;
            }
            else
            {
                updateInfo.updateProgress_fs = percent;
            }
        });
    #endif

    bool fsUpdateResult = true;
    if(updateInfo.has_fs_update)
    {
        currentlyUpdatingFs = true;
        #ifdef DEBUG_OUTPUT
            Serial.println("Update file system...");
        #endif
        t_httpUpdate_return returnFsUpdate = ESPhttpUpdate.updateFS(client, updateInfo.url_fs);
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
    t_httpUpdate_return returnFwUpdate = ESPhttpUpdate.update(client, updateInfo.url_fw);
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

void updateHandling_clearVersionInfo()
{
    updateInfo.valid = false;
    updateInfo.version = "";
    updateInfo.url_fw = "";
    updateInfo.url_fs = "";
    updateInfo.url_fw_sha256 = "";
    updateInfo.url_fs_sha256 = "";
    updateInfo.has_fs_update = false;
    updateInfo.updateProgress_fw = 0.0f;
    updateInfo.updateProgress_fs = 0.0f;
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
        doc["available"] = updateInfo.valid;
        doc["has_fs_update"] = updateInfo.has_fs_update;
        doc["version"] = updateInfo.version;
        doc["url_fw"] = updateInfo.url_fw;
        doc["url_fs"] = updateInfo.url_fs;
        doc["url_fw_sha256"] = updateInfo.url_fw_sha256;
        doc["url_fs_sha256"] = updateInfo.url_fs_sha256;
        doc["updateProgress_fw"] = updateInfo.updateProgress_fw;
        doc["updateProgress_fs"] = updateInfo.updateProgress_fs;
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
    else if(isUpdating)
    {
        updateHandling_performUpdate();
        isUpdating = false;
    }
}