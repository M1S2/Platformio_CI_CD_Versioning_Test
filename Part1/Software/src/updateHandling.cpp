#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include "updateHandling.h"
#include "wifiHandling.h"
#include "timeHandling.h"
#include "certs.h"
#include "version.h"
#include "updateHandling_Part1.h"
#include "updateHandling_Part2.h"

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
  "part1_fs_md5": "0b37d70272041137295d7dc4ca508698",
  "part2_fw": "part2_fw.bin",
  "part2_fw_md5": "50bff09cec367445d75cee900608b86d"
}

Dev Manifest Format:
{
  "version": "dev-SW_v3.0.0-p3-9853261",
  "part1_fw": "part1_fw.bin",
  "part1_fs": "part1_fs.bin",
  "part1_fw_md5": "4375b4f6083364c9dcd557f80cadd149",
  "part1_fs_md5": "0b37d70272041137295d7dc4ca508698",
  "part2_fw": "part2_fw.bin",
  "part2_fw_md5": "50bff09cec367445d75cee900608b86d"
}
*/

// Create a list of certificates with the server certificate
X509List certList(ROOT_CA_CERT);

update_status_t updateStatus;

update_info_t updateInfo_Part1;
update_info_t updateInfo_Part2;
update_info_t* updateInfos[] = { &updateInfo_Part1, &updateInfo_Part2 };

const char* componentNames[] = { UPDATE_COMPONENT_NAME_PART1, UPDATE_COMPONENT_NAME_PART2 };

// Current versions arrays for each component
const char* currentVersionsPart1[] = { FW_VERSION };
const char* currentVersionsPart2[] = { "?", "?" };
const char** currentVersionsArray[] = { currentVersionsPart1, currentVersionsPart2 };
const size_t currentVersionsCounts[] = { sizeof(currentVersionsPart1) / sizeof(currentVersionsPart1[0]), 
                                        sizeof(currentVersionsPart2) / sizeof(currentVersionsPart2[0]) };

bool requestNewVersionCheck = false;
bool requestUpdate = false;

/**********************************************************************/

void updateHandling_sendProgressEvent(float progress)
{
    String payload = String(progress, 2);
    events.send(payload.c_str(), SERVER_EVENT_UPDATE_PROGRESS);
}

String updateHandling_getUpdateStatusJson(update_status_t &status)
{
    DynamicJsonDocument doc(512);
    doc["channel"] = status.currentUpdateChannel;
    doc["state"] = status.state;
    doc["currentComponent"] = status.currentComponent;
    doc["currentComponentInstanceIndex"] = status.currentComponentInstanceIndex;
    doc["updateStep"] = status.updateStep;
    doc["updateProgress"] = status.updateProgress;

    String response;
    serializeJson(doc, response);
    return response;
}

void updateHandling_sendUpdateStatusEvent()
{
    String response = updateHandling_getUpdateStatusJson(updateStatus);
    events.send(response.c_str(), SERVER_EVENT_UPDATE_STATUS);
}

void updateHandling_setUpdateStep(UpdateStep step)
{
    updateStatus.updateStep = step;
    updateHandling_sendUpdateStatusEvent();
}

/**********************************************************************/

bool updateHandling_findComponentByName(String componentName, update_info_t* foundUpdateInfo = nullptr, int* foundIndex = nullptr)
{
    #ifdef DEBUG_OUTPUT
        Serial.printf("Looking for component \"%s\"...\n", componentName.c_str());
    #endif

    bool componentValid = false;
    if(foundIndex != nullptr) { *foundIndex = -1; }
    size_t count = sizeof(componentNames) / sizeof(componentNames[0]);
    for (size_t i = 0; i < count; i++)
    {
        if (componentName == componentNames[i])
        {
            componentValid = true;
            if(foundIndex != nullptr) { *foundIndex = i; }
            if (foundUpdateInfo != nullptr)
            {
                *foundUpdateInfo = *updateInfos[i];
            }

            #ifdef DEBUG_OUTPUT
                Serial.printf("Found component \"%s\" at index %d\n", componentName.c_str(), i);
            #endif
            break;
        }
    }
    return componentValid;
}

/**********************************************************************/

bool updateHandling_fetchVersions(update_info_t *infos[], const char* componentNames[], size_t count)
{
    if(isTimeValid == false)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("Time is not valid yet, cannot check for updates because SSL certificate validation will fail. Try again later...");
        #endif
        return false;
    }

    if (count == 0 || infos == nullptr || componentNames == nullptr)
    {
        return false;
    }

    const char* baseUrl = (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? stableBaseUrl : devBaseUrl;
    String manifestUrl = String(baseUrl) + manifestFilename;

    #ifdef DEBUG_OUTPUT
        Serial.printf("Checking for %s update...\n", (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev");
    #endif

    WiFiClientSecure client;
    client.setTrustAnchors(&certList);

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

    bool anyValid = false;
    for (size_t i = 0; i < count; ++i)
    {
        update_info_t &info = *infos[i];
        const String componentName = String(componentNames[i]);
        info.componentName = componentName;
        info.valid = false;
        info.version = "";
        info.url_fw = "";
        info.url_fs = "";
        info.fw_md5 = "";
        info.fs_md5 = "";
        info.has_fs_update = false;

        String keyVersion = "version";
        String keyFw = componentName + "_fw";
        String keyFs = componentName + "_fs";
        String keyFwMd5 = componentName + "_fw_md5";
        String keyFsMd5 = componentName + "_fs_md5";

        if(doc.containsKey(keyVersion))
        {
            info.version = doc[keyVersion].as<String>();
        }
        if (doc.containsKey(keyFw) && doc.containsKey(keyFwMd5))
        {
            info.url_fw = String(baseUrl) + doc[keyFw].as<String>();
            info.fw_md5 = doc[keyFwMd5].as<String>();
            info.valid = true;
            anyValid = true;
        }
        if(doc.containsKey(keyFs) && doc.containsKey(keyFsMd5))
        {
            info.url_fs = String(baseUrl) + doc[keyFs].as<String>();
            info.fs_md5 = doc[keyFsMd5].as<String>();
            info.has_fs_update = true;
        }
    }

    return anyValid;
}

/**********************************************************************/

bool updateHandling_performUpdate(String component = "", int componentInstanceIndex = -1)
{
    updateStatus.updateStep = UPDATE_STEP_NONE;

    update_info_t updateInfo;
    if(!updateHandling_findComponentByName(component, &updateInfo, nullptr))
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("No update info found for component \"%s\"\n", component.c_str());
        #endif
        return false;
    }

    if(component == UPDATE_COMPONENT_NAME_PART1)
    {
        return updateHandling_performUpdatePart1(updateInfo, component, componentInstanceIndex);
    }
    else if(component == UPDATE_COMPONENT_NAME_PART2)
    {
        return updateHandling_performUpdatePart2(updateInfo, component, componentInstanceIndex);
    }
    else
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("Update for component \"%s\" not supported\n", component.c_str());
        #endif
        return false;
    }
}

/**********************************************************************/

void updateHandling_clearVersionInfos()
{
    size_t count = sizeof(updateInfos) / sizeof(updateInfos[0]);
    for (size_t i = 0; i < count; ++i)
    {   
        updateInfos[i]->valid = false;
        updateInfos[i]->version = "";
        updateInfos[i]->url_fw = "";
        updateInfos[i]->url_fs = "";
        updateInfos[i]->fw_md5 = "";
        updateInfos[i]->fs_md5 = "";
        updateInfos[i]->has_fs_update = false;
    }
}

/**********************************************************************/

void updateHandling_initWebserverEndpoints()
{
    server.on("/update/set_channel", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if(updateStatus.state != UPDATE_STATE_IDLE && updateStatus.state != UPDATE_STATE_ERROR)
        {
            request->send(400, "text/plain", "Cannot change update channel while fetching version infos or performing an update");
            return;
        }

        String channel = "";
        if (request->hasParam("channel"))
        {
            channel = request->getParam("channel")->value();
        }

        if (channel == "dev")
        {
            updateStatus.currentUpdateChannel = UPDATE_CHANNEL_DEV;
        }
        else if (channel == "stable")
        {
            updateStatus.currentUpdateChannel = UPDATE_CHANNEL_STABLE;
        }
        else
        {
            request->send(400, "text/plain", "Missing or invalid channel parameter");
            return;
        }

        updateHandling_clearVersionInfos();
        request->send(200, "text/plain", "Channel set to " + channel);
    });

    server.on("/update/check", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if(updateStatus.state != UPDATE_STATE_IDLE && updateStatus.state != UPDATE_STATE_ERROR)
        {
            request->send(400, "text/plain", "Already fetching version infos or performing an update");
            return;
        }
        else
        {
            updateHandling_startFetchingNewestVersionInfos();
            request->send(200, "text/plain", "Check for updates...");
        }
    });

    server.on("/update/start", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        String component = "";
        if (request->hasParam("component"))
        {
            component = request->getParam("component")->value();
        }
        
        String componentInstanceIndexStr = "";
        int componentInstanceIndex = -1;
        if(request->hasParam("componentInstanceIndex"))
        {
            componentInstanceIndexStr = request->getParam("componentInstanceIndex")->value();
            componentInstanceIndex = componentInstanceIndexStr.toInt();
        }

        DynamicJsonDocument doc(512);
        int resultCode = 200;
        bool componentValid = updateHandling_findComponentByName(component);
        if (componentValid)
        {
            updateHandling_startUpdate(component, componentInstanceIndex);
            doc["status"] = "ok";
            doc["message"] = "Update for " + component + ", index " + componentInstanceIndexStr + " started";
        }
        else
        {
            doc["status"] = "error";
            doc["message"] = "Invalid component \"" + component + "\"";
            resultCode = 400;
        }

        String response;
        serializeJson(doc, response);
        request->send(resultCode, "application/json", response);
    });

    server.on("/update/status", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        String response = updateHandling_getUpdateStatusJson(updateStatus);
        request->send(200, "application/json", response);
    });

    server.on("/update/info", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        DynamicJsonDocument doc(2048);
        JsonArray componentsArray = doc.createNestedArray("components");

        size_t count = sizeof(updateInfos) / sizeof(updateInfos[0]);
        for (size_t i = 0; i < count; ++i)
        {
            JsonObject component = componentsArray.createNestedObject();
            component["name"] = componentNames[i];
            component["available"] = updateInfos[i]->valid;
            component["has_fs_update"] = updateInfos[i]->has_fs_update;
            component["version"] = updateInfos[i]->version;
            component["url_fw"] = updateInfos[i]->url_fw;
            component["fw_md5"] = updateInfos[i]->fw_md5;
            component["url_fs"] = updateInfos[i]->url_fs;
            component["fs_md5"] = updateInfos[i]->fs_md5;
            
            JsonArray versionsArray = component.createNestedArray("currentVersions");
            for (size_t j = 0; j < currentVersionsCounts[i]; ++j)
            {
                versionsArray.add(currentVersionsArray[i][j]);
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    updateHandling_initWebserverEndpoints_Part2();
}

/**********************************************************************/

void updateHandling_startFetchingNewestVersionInfos()
{
    updateHandling_clearVersionInfos();
    updateStatus.currentComponent = "";
    // Set state to fetch the newest version infos in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    updateStatus.state = UPDATE_STATE_CHECKING;
    updateHandling_sendUpdateStatusEvent();
}

void updateHandling_startUpdate(String component, int componentInstanceIndex)
{
    updateStatus.currentComponent = component;
    updateStatus.currentComponentInstanceIndex = componentInstanceIndex;
    // Set state to perform the update in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server or by performing the update itself, which can take a long time)
    updateStatus.state = UPDATE_STATE_UPDATING;
    updateHandling_sendUpdateStatusEvent();
}

/**********************************************************************/

void updateHandling_loop()
{
    if(updateStatus.state == UPDATE_STATE_CHECKING)
    {
        size_t count = sizeof(updateInfos) / sizeof(updateInfos[0]);
        bool result = updateHandling_fetchVersions(updateInfos, componentNames, count);

        #ifdef DEBUG_OUTPUT
            if(result)
            {
                for(size_t i = 0; i < count; ++i)
                {
                    update_info_t &info = *updateInfos[i];
                    Serial.printf("Component: %s\n", info.componentName.c_str());
                    Serial.printf("  Valid: %s\n", info.valid ? "true" : "false");
                    Serial.printf("  Version: %s\n", info.version.c_str());
                    Serial.printf("  Has FS Update: %s\n", info.has_fs_update ? "true" : "false");
                    Serial.printf("  URL FW: %s\n", info.url_fw.c_str());
                    Serial.printf("  MD5 FW: %s\n", info.fw_md5.c_str());
                    if(info.has_fs_update)
                    {
                        Serial.printf("  URL FS: %s\n", info.url_fs.c_str());
                        Serial.printf("  MD5 FS: %s\n", info.fs_md5.c_str());
                    }
                }
            }
            else
            {
                Serial.println("Fetching version infos failed");
            }
        #endif
        updateStatus.state = result ? UPDATE_STATE_IDLE : UPDATE_STATE_ERROR;
        updateHandling_sendUpdateStatusEvent();
    }
    else if(updateStatus.state == UPDATE_STATE_UPDATING)
    {
        bool result = updateHandling_performUpdate(updateStatus.currentComponent, updateStatus.currentComponentInstanceIndex);
        updateStatus.state = result ? UPDATE_STATE_IDLE : UPDATE_STATE_ERROR;
        updateHandling_sendUpdateStatusEvent();
    }
}