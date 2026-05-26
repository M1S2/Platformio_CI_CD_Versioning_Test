#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
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

UpdateQueue updateQueue;
update_task_t currentUpdateTask;

/**********************************************************************/

bool UpdateQueue::push(const update_task_t &task)
{
    if (count >= UPDATE_QUEUE_SIZE)
    {
        return false;
    }

    queue[tail] = task;
    tail = (tail + 1) % UPDATE_QUEUE_SIZE;
    count++;
    return true;
}

bool UpdateQueue::pop(update_task_t &task)
{
    if (count == 0)
    {
        return false;
    }

    task = queue[head];
    head = (head + 1) % UPDATE_QUEUE_SIZE;
    count--;
    return true;
}

bool UpdateQueue::isEmpty() const
{
    return count == 0;
}

size_t UpdateQueue::size() const
{
    return count;
}

void UpdateQueue::clear()
{
    head = 0;
    tail = 0;
    count = 0;
}

/**********************************************************************/

bool updateHandling_findComponentByName(String componentName, update_info_t* foundUpdateInfo = nullptr, int* foundIndex = nullptr)
{
    #ifdef DEBUG_OUTPUT
        Serial.printf_P(PSTR("[Update Handling] Looking for component \"%s\"...\n"), componentName.c_str());
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
                Serial.printf_P(PSTR("[Update Handling] Found component \"%s\" at index %d\n"), componentName.c_str(), i);
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
            Serial.println(F("[Update Handling] Time is not valid yet, cannot check for updates because SSL certificate validation will fail. Try again later..."));
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
        Serial.printf_P(PSTR("[Update Handling] Checking for %s update...\n"), (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev");
    #endif

    WiFiClientSecure clientSecure;
    clientSecure.setSession(&session);
    clientSecure.setTrustAnchors(&certList);
    clientSecure.setBufferSizes(1024, 512);

    HTTPClient http;
    http.setTimeout(10000); // 10 Seconds
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    http.begin(clientSecure, manifestUrl.c_str());
    int httpCode = http.GET();
    #ifdef DEBUG_OUTPUT
        if (httpCode <= 0)
        {
            Serial.printf_P(PSTR("[Update Handling] HTTP error: Code = %d, Message = %s\n"), httpCode, http.errorToString(httpCode).c_str());
        }
    #endif

    if (httpCode != 200)
    {
        http.end();
        return false;
    }

    // Parse directly from stream to save stack and heap memory
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

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

    int foundIndex = -1;
    if(!updateHandling_findComponentByName(component, nullptr, &foundIndex))
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling] No update info found for component \"%s\"\n"), component.c_str());
        #endif
        return false;
    }
    update_info_t& updateInfo = *updateInfos[foundIndex];

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
            Serial.printf_P(PSTR("[Update Handling] Update for component \"%s\" not supported\n"), component.c_str());
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

void updateHandling_prepareStatusDoc(const update_status_t &status, JsonDocument &doc)
{
    doc["channel"] = status.currentUpdateChannel;
    doc["state"] = status.state;
    doc["currentComponent"] = status.currentComponent;
    doc["currentComponentInstanceIndex"] = status.currentComponentInstanceIndex;
    doc["updateStep"] = status.updateStep;
    doc["updateProgress"] = status.updateProgress;
}

/**********************************************************************/

void updateHandling_initWebserverEndpoints()
{
    // Handler for /update/set_channel (POST with JSON-Body)
    static AsyncCallbackJsonWebHandler* setChannelHandler = nullptr;
    if (!setChannelHandler)
    {
        setChannelHandler = new AsyncCallbackJsonWebHandler("/update/set_channel", [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            if(updateStatus.state != UPDATE_STATE_IDLE && updateStatus.state != UPDATE_STATE_ERROR)
            {
                request->send(400, "text/plain", "Cannot change update channel while fetching version infos or performing an update");
                return;
            }

            JsonObject jsonObj = json.as<JsonObject>();
            if (!jsonObj.containsKey("channel"))
            {
                request->send(400, "text/plain", "Missing 'channel' parameter in JSON body");
                return;
            }

            String channel = jsonObj["channel"].as<String>();
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
        server.addHandler(setChannelHandler);
    }

    /*--------------------------------------------------------------------*/

    server.on("/update/check", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        if(updateStatus.state != UPDATE_STATE_IDLE && updateStatus.state != UPDATE_STATE_ERROR)
        {
            request->send(400, "text/plain", "Already fetching version infos or performing an update.");
            return;
        }
        else
        {
            updateHandling_startFetchingNewestVersionInfos();
            request->send(200, "text/plain", "Check for updates initiated.");
        }
    });

    /*--------------------------------------------------------------------*/

    // Handler for /update/start (POST with JSON-Body)
    static AsyncCallbackJsonWebHandler* startUpdateHandler = nullptr;
    if (!startUpdateHandler)
    {
        startUpdateHandler = new AsyncCallbackJsonWebHandler("/update/start", [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject jsonObj = json.as<JsonObject>();
            if (!jsonObj.containsKey("component") || !jsonObj.containsKey("componentInstanceIndex"))
            {
                request->send(400, "text/plain", "Missing 'component' or 'componentInstanceIndex' in JSON body");
                return;
            }

            String component = jsonObj["component"].as<String>();
            int componentInstanceIndex = jsonObj["componentInstanceIndex"].as<int>();

            StaticJsonDocument<128> doc;
            int resultCode = 200;
            bool componentValid = updateHandling_findComponentByName(component);
            if (componentValid)
            {
                updateHandling_startUpdate(component, componentInstanceIndex);
                doc["status"] = "ok";
                doc["message"] = "Update for " + component + " started";
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
        server.addHandler(startUpdateHandler);
    }

    /*--------------------------------------------------------------------*/

    server.on("/update/status", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        StaticJsonDocument<128> doc;
        updateHandling_prepareStatusDoc(updateStatus, doc);
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    /*--------------------------------------------------------------------*/

    server.on("/update/info", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // Using a pointer for the doc to keep stack usage minimal
        StaticJsonDocument<1024> doc;
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

    /*--------------------------------------------------------------------*/

    updateHandling_initWebserverEndpoints_Part2();
}

/**********************************************************************/

void updateHandling_startFetchingNewestVersionInfos()
{
    updateHandling_clearVersionInfos();
    updateStatus.currentComponent = "";
    // Set state to fetch the newest version infos in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    updateStatus.state = UPDATE_STATE_CHECKING;
}

void updateHandling_startUpdate(String component, int componentInstanceIndex)
{
    update_task_t newTask;
    newTask.component = component;
    newTask.componentInstanceIndex = componentInstanceIndex;
    updateQueue.push(newTask);
}

/**********************************************************************/

void updateHandling_loop()
{
    switch (updateStatus.state)
    {
        case UPDATE_STATE_IDLE:
        {
            if(!updateQueue.isEmpty())
            {
                updateQueue.pop(currentUpdateTask);
                updateStatus.state = UPDATE_STATE_UPDATING;
            }
            break;
        }
        case UPDATE_STATE_CHECKING:
        {
            size_t count = sizeof(updateInfos) / sizeof(updateInfos[0]);
            bool result = updateHandling_fetchVersions(updateInfos, componentNames, count);

            #ifdef DEBUG_OUTPUT
                if(result)
                {
                    Serial.println(F("[Update Handling] Fetching version infos successful:"));
                    for(size_t i = 0; i < count; ++i)
                    {
                        update_info_t &info = *updateInfos[i];
                        Serial.printf_P(PSTR("Component: %s\n"), info.componentName.c_str());
                        Serial.printf_P(PSTR("  Valid: %s\n"), info.valid ? "true" : "false");
                        Serial.printf_P(PSTR("  Version: %s\n"), info.version.c_str());
                        Serial.printf_P(PSTR("  Has FS Update: %s\n"), info.has_fs_update ? "true" : "false");
                        Serial.printf_P(PSTR("  URL FW: %s\n"), info.url_fw.c_str());
                        Serial.printf_P(PSTR("  MD5 FW: %s\n"), info.fw_md5.c_str());
                        if(info.has_fs_update)
                        {
                            Serial.printf_P(PSTR("  URL FS: %s\n"), info.url_fs.c_str());
                            Serial.printf_P(PSTR("  MD5 FS: %s\n"), info.fs_md5.c_str());
                        }
                    }
                }
                else
                {
                    Serial.println(F("[Update Handling] Fetching version infos failed"));
                }
            #endif
            updateStatus.state = result ? UPDATE_STATE_IDLE : UPDATE_STATE_ERROR;
            break;
        }
        case UPDATE_STATE_UPDATING:
        {
            // Get the parameters for the update from the currentUpdateTask struct, which should have been set by the HTTP request handler for /update/start before switching the state to UPDATE_STATE_UPDATING
            updateStatus.currentComponent = currentUpdateTask.component;
            updateStatus.currentComponentInstanceIndex = currentUpdateTask.componentInstanceIndex;

            bool result = updateHandling_performUpdate(updateStatus.currentComponent, updateStatus.currentComponentInstanceIndex);
            updateStatus.state = result ? UPDATE_STATE_IDLE : UPDATE_STATE_ERROR;
            break;
        }
        case UPDATE_STATE_ERROR:
        {
            updateQueue.clear();
            updateStatus.currentComponent = "";
            updateStatus.currentComponentInstanceIndex = -1;
            break;
        }
        default: break;
    }
}