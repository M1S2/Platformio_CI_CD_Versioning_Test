#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include "updateHandling.h"
#include "updateHandlingConfig.h"

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

bool requestNewVersionCheck = false;
bool requestUpdate = false;

UpdateHandling updateHandling(UPDATE_STABLEBASEURL, UPDATE_DEVBASEURL, UPDATE_MANIFESTFILENAME);

UpdateHandling::UpdateHandling(const char* stableBaseUrl, const char* devBaseUrl, const char* manifestName)
{
    this->stableBaseUrl = stableBaseUrl;
    this->devBaseUrl = devBaseUrl;
    this->manifestName = manifestName;
}

/**********************************************************************/

UpdateComponents UpdateHandling::findComponentByName(const String& componentName) const
{
    for (size_t componentIndex = 0; componentIndex < updateComponentDefinitionCount; ++componentIndex)
    {
        const update_component_definition_t &definition = updateComponentDefinitions[componentIndex];
        if(definition.componentName == componentName)
        {
            return definition.component;
        }
    }
    return UPDATE_COMPONENT_NONE;
}

/**********************************************************************/

bool UpdateHandling::fetchVersions()
{
    if (updateComponentDefinitionCount == 0)
    {
        return false;
    }

    const char* baseUrl = (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? stableBaseUrl : devBaseUrl;
    String manifestUrl = String(baseUrl) + manifestName;

    #ifdef DEBUG_OUTPUT
        Serial.printf_P(PSTR("[Update Handling] Checking for %s update...\n"), (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev");
    #endif

    WiFiClientSecure clientSecure;
    clientSecure.setSession(p_wifiSession);
    clientSecure.setTrustAnchors(p_wifiCertList);
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
    for (size_t i = 0; i < updateComponentDefinitionCount; ++i)
    {
        if(updateComponentDefinitions[i].updateInfo == nullptr) { continue; }
        update_info_t &info = *updateComponentDefinitions[i].updateInfo;
        const String componentName = updateComponentDefinitions[i].componentName;
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

void UpdateHandling::clearVersionInfos()
{
    for (size_t i = 0; i < updateComponentDefinitionCount; ++i)
    {
        if(updateComponentDefinitions[i].updateInfo == nullptr) { continue; }
        update_info_t &info = *updateComponentDefinitions[i].updateInfo;
        info.valid = false;
        info.version = "";
        info.url_fw = "";
        info.url_fs = "";
        info.fw_md5 = "";
        info.fs_md5 = "";
        info.has_fs_update = false;
    }
}

/**********************************************************************/

void UpdateHandling::prepareStatusDoc(const update_status_t &status, JsonDocument &doc)
{
    doc["channel"] = status.currentUpdateChannel;
    doc["state"] = status.state;
    doc["currentComponent"] = status.currentComponent;
    doc["currentComponentInstanceIndex"] = status.currentComponentInstanceIndex;
    doc["updateStep"] = status.updateStep;
    doc["updateProgress"] = status.updateProgress;
}

/**********************************************************************/

void UpdateHandling::initWebserverEndpoints(AsyncWebServer* p_server, Session* p_session, X509List* p_certList)
{
    p_wifiSession = p_session;
    p_wifiCertList = p_certList;

    /*--------------------------------------------------------------------*/

    // Handler for /update/set_channel (POST with JSON-Body)
    static AsyncCallbackJsonWebHandler* setChannelHandler = nullptr;
    if (!setChannelHandler)
    {
        setChannelHandler = new AsyncCallbackJsonWebHandler("/update/set_channel", [this](AsyncWebServerRequest *request, JsonVariant &json)
        {
            if(this->updateStatus.state != UPDATE_STATE_IDLE && this->updateStatus.state != UPDATE_STATE_ERROR)
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
                this->updateStatus.currentUpdateChannel = UPDATE_CHANNEL_DEV;
            }
            else if (channel == "stable")
            {
                this->updateStatus.currentUpdateChannel = UPDATE_CHANNEL_STABLE;
            }
            else
            {
                request->send(400, "text/plain", "Missing or invalid channel parameter");
                return;
            }

            this->clearVersionInfos();
            request->send(200, "text/plain", "Channel set to " + channel);
        });
        p_server->addHandler(setChannelHandler);
    }

    /*--------------------------------------------------------------------*/

    p_server->on("/update/check", HTTP_POST, [this](AsyncWebServerRequest *request)
    {
        if(this->startFetchingNewestVersionInfos())
        {
            request->send(200, "text/plain", "Check for updates initiated.");
        }
        else
        {
            request->send(400, "text/plain", "Already fetching version infos or performing an update.");
        }
    });

    /*--------------------------------------------------------------------*/

    // Handler for /update/start (POST with JSON-Body)
    static AsyncCallbackJsonWebHandler* startUpdateHandler = nullptr;
    if (!startUpdateHandler)
    {
        startUpdateHandler = new AsyncCallbackJsonWebHandler("/update/start", [this](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject jsonObj = json.as<JsonObject>();
            if (!jsonObj.containsKey("component") || !jsonObj.containsKey("componentInstanceIndex"))
            {
                request->send(400, "text/plain", "Missing 'component' or 'componentInstanceIndex' in JSON body");
                return;
            }

            String componentStr = jsonObj["component"].as<String>();
            int componentInstanceIndex = jsonObj["componentInstanceIndex"].as<int>();

            StaticJsonDocument<128> doc;
            int resultCode = 200;
            UpdateComponents component = this->findComponentByName(componentStr);
            if (component != UPDATE_COMPONENT_NONE)
            {
                this->enqueueUpdateTasks(component, componentInstanceIndex);
                doc["status"] = "ok";
                doc["message"] = "Update for " + componentStr + " started";
            }
            else
            {
                doc["status"] = "error";
                doc["message"] = "Invalid component \"" + componentStr + "\"";
                resultCode = 400;
            }

            String response;
            serializeJson(doc, response);
            request->send(resultCode, "application/json", response);
        });
        p_server->addHandler(startUpdateHandler);
    }

    /*--------------------------------------------------------------------*/

    p_server->on("/update/status", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        StaticJsonDocument<128> doc;
        this->prepareStatusDoc(this->updateStatus, doc);
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    /*--------------------------------------------------------------------*/

    p_server->on("/update/info", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        StaticJsonDocument<1024> doc;
        JsonArray componentsArray = doc.createNestedArray("components");

        for (size_t componentIndex = 0; componentIndex < updateComponentDefinitionCount; ++componentIndex)
        {
            if(updateComponentDefinitions[componentIndex].updateInfo == nullptr) { continue; }
            update_component_definition_t definition = updateComponentDefinitions[componentIndex];
            update_info_t &info = *definition.updateInfo;
            size_t instanceCount = definition.getInstanceCountHandler();

            JsonObject component = componentsArray.createNestedObject();
            component["id"] = definition.component;
            component["name"] = definition.componentName;
            component["available"] = info.valid;
            component["has_fs_update"] = info.has_fs_update;
            component["version"] = info.version;
            component["url_fw"] = info.url_fw;
            component["fw_md5"] = info.fw_md5;
            component["url_fs"] = info.url_fs;
            component["fs_md5"] = info.fs_md5;
            component["instance_count"] = instanceCount;

            JsonArray versionsArray = component.createNestedArray("currentVersions");
            for (size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
            {
                versionsArray.add(definition.queryVersionHandler(instanceIndex));
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    /*--------------------------------------------------------------------*/

    p_server->on("/update/remaining_tasks", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        StaticJsonDocument<JSON_ARRAY_SIZE(UPDATE_QUEUE_SIZE) + UPDATE_QUEUE_SIZE * JSON_OBJECT_SIZE(3)> doc;

        JsonArray array = doc.to<JsonArray>();

        size_t qSize = this->updateTaskQueue.size();
        for (size_t i = 0; i < qSize; i++)
        {
            update_task_t task;
            if (this->updateTaskQueue.getAt(i, task))
            {
                JsonObject obj = array.createNestedObject();
                obj["component"] = task.component;
                obj["instance"] = task.componentInstanceIndex;
                obj["step"] = task.step;
            }
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    /*--------------------------------------------------------------------*/

    for (size_t componentIndex = 0; componentIndex < updateComponentDefinitionCount; ++componentIndex)
    {
        const update_component_definition_t &definition = updateComponentDefinitions[componentIndex];
        if (definition.initWebserverEndpointsHandler)
        {
            definition.initWebserverEndpointsHandler(p_server);
        }
    }
}

/**********************************************************************/

bool UpdateHandling::startFetchingNewestVersionInfos()
{
    if(updateStatus.state != UPDATE_STATE_IDLE && updateStatus.state != UPDATE_STATE_ERROR)
    {
        return false;
    }
    clearVersionInfos();
    updateStatus.currentComponent = UPDATE_COMPONENT_NONE;
    // Set state to fetch the newest version infos in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    updateStatus.state = UPDATE_STATE_CHECKING;
    return true;
}

/**********************************************************************/

bool UpdateHandling::enqueueSingleUpdateTask(UpdateComponents component, int instanceIndex, UpdateSteps step, update_step_handler_t handler)
{
    update_task_t task;
    task.component = component;
    task.componentInstanceIndex = instanceIndex;
    task.step = step;
    task.handler = handler;
    return updateTaskQueue.push(task);
}


void UpdateHandling::enqueueUpdateTasks(UpdateComponents component, int componentInstanceIndex)
{
    bool found = false;
    for(size_t i = 0; i < updateComponentDefinitionCount; i++)
    {
        if(updateComponentDefinitions[i].component == component)
        {
            const update_component_definition_t &definition = updateComponentDefinitions[i];
            definition.enqueueHandler(componentInstanceIndex);
            found = true;
            break;
        }
    }
    if(!found)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling] Cannot enqueue update tasks: Component \"%s\" not supported\n"), component);
        #endif
        return;
    }
}

/**********************************************************************/

void UpdateHandling::loop()
{
    switch (updateStatus.state)
    {
        case UPDATE_STATE_IDLE:
        {
            if(!updateTaskQueue.isEmpty())
            {
                // Change to UPDATING state to perform the next update task in the queue in the next loop() iteration
                updateStatus.state = UPDATE_STATE_UPDATING;
            }
            break;
        }
        case UPDATE_STATE_CHECKING:
        {
            bool result = fetchVersions();

            #ifdef DEBUG_OUTPUT
                if(result)
                {
                    Serial.println(F("[Update Handling] Fetching version infos successful:"));
                    for(size_t i = 0; i < updateComponentDefinitionCount; ++i)
                    {
                        if(updateComponentDefinitions[i].updateInfo == nullptr) { continue; }
                        update_info_t &info = *updateComponentDefinitions[i].updateInfo;
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
            if(!updateTaskQueue.isEmpty())
            {
                updateTaskQueue.pop(currentUpdateTask);

                updateStatus.currentComponent = currentUpdateTask.component;
                updateStatus.currentComponentInstanceIndex = currentUpdateTask.componentInstanceIndex;
                updateStatus.updateStep = currentUpdateTask.step;
                if(currentUpdateTask.handler == nullptr)
                {
                    #ifdef DEBUG_OUTPUT
                        Serial.println(F("[Update Handling] No handler assigned"));
                    #endif
                    updateStatus.state = UPDATE_STATE_ERROR;
                }
                else
                {
                    #ifdef DEBUG_OUTPUT
                        Serial.printf_P(PSTR("[Update Handling] Performing update task: Component = %d, Instance Index = %d, Step = %d\n"), currentUpdateTask.component, currentUpdateTask.componentInstanceIndex, currentUpdateTask.step);
                    #endif
                    bool result = currentUpdateTask.handler(currentUpdateTask);
                    if(!result)
                    {
                        updateStatus.state = UPDATE_STATE_ERROR;
                    }
                }
            }
            else
            {
                // No task in the queue, go back to IDLE state
                updateStatus.updateStep = UPDATE_STEP_FINISHED;
                updateStatus.state = UPDATE_STATE_IDLE;
            }
            break;
        }
        case UPDATE_STATE_ERROR:
        {
            updateTaskQueue.clear();
            updateStatus.currentComponent = UPDATE_COMPONENT_NONE;
            updateStatus.currentComponentInstanceIndex = -1;
            updateStatus.updateStep = UPDATE_STEP_NONE;
            updateStatus.updateProgress = 0.0f;
            break;
        }
        default: break;
    }
}