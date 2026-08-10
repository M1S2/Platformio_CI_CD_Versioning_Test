#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include "updateHandling.h"

/*
Stable Manifest Format:
{
  "version": "3.0.0",
  "local_fw": "local_fw_3.0.0.bin",
  "local_fs": "local_fs_3.0.0.bin",
  "local_fw_md5": "4375b4f6083364c9dcd557f80cadd149",
  "local_fs_md5": "0b37d70272041137295d7dc4ca508698",
  "remote_fw": "remote_fw_3.0.0.bin",
  "remote_fw_md5": "50bff09cec367445d75cee900608b86d"
}

Dev Manifest Format:
{
  "version": "dev-SW_v3.0.0-p3-9853261",
  "local_fw": "local_fw.bin",
  "local_fs": "local_fs.bin",
  "local_fw_md5": "4375b4f6083364c9dcd557f80cadd149",
  "local_fs_md5": "0b37d70272041137295d7dc4ca508698",
  "remote_fw": "remote_fw.bin",
  "remote_fw_md5": "50bff09cec367445d75cee900608b86d"
}
*/

bool requestNewVersionCheck = false;
bool requestUpdate = false;

UpdateHandling::UpdateHandling(const char* stableBaseUrl, const char* devBaseUrl, const char* manifestName, log_function_t logFunction) :
    logFunction(logFunction), stableBaseUrl(stableBaseUrl), devBaseUrl(devBaseUrl), manifestName(manifestName)
{
}

/**********************************************************************/

UpdateHandling::~UpdateHandling()
{
    if (this->updateComponents)
    {
        delete[] this->updateComponents;
        this->updateComponents = nullptr;
    }
    this->updateComponentCount = 0;
}

bool UpdateHandling::registerComponent(UpdateHandlingComponentBase* component)
{
    if (component == nullptr) return false;

    for (size_t i = 0; i < this->updateComponentCount; ++i)
    {
        if (this->updateComponents[i] == component) return true;
    }

    UpdateHandlingComponentBase** newArray = new UpdateHandlingComponentBase*[this->updateComponentCount + 1];
    for (size_t i = 0; i < this->updateComponentCount; ++i)
    {
        newArray[i] = this->updateComponents[i];
    }
    newArray[this->updateComponentCount] = component;

    if (this->updateComponents)
    {
        delete[] this->updateComponents;
    }
    this->updateComponents = newArray;
    ++this->updateComponentCount;
    component->p_updateHandling = this;
    return true;
}

size_t UpdateHandling::getComponentCount() const
{
    return this->updateComponentCount;
}

UpdateHandlingComponentBase* UpdateHandling::getComponentAt(size_t index) const
{
    return (index < this->updateComponentCount) ? this->updateComponents[index] : nullptr;
}

UpdateHandlingComponentBase* UpdateHandling::getComponentByName(String componentName)
{
    for(size_t i = 0; i < this->getComponentCount(); i++)
    {
        UpdateHandlingComponentBase* componentDef = this->getComponentAt(i);
        if(componentDef != nullptr && componentDef->componentName == componentName)
        {
            return componentDef;
        }
    }
    return nullptr;
}

void UpdateHandling::log(PGM_P formatP, ...)
{
    if (this->logFunction != nullptr)
    {
        va_list args;
        va_start(args, formatP);
        this->logFunction(formatP, args);
        va_end(args);
    }
}

/**********************************************************************/

bool UpdateHandling::fetchVersions()
{
    if (this->getComponentCount() == 0)
    {
        return false;
    }

    const char* baseUrl = (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? stableBaseUrl : devBaseUrl;
    String manifestUrl = String(baseUrl) + manifestName;

    log(PSTR("[Update Handling] Checking for %s update...\n"), (updateStatus.currentUpdateChannel == UPDATE_CHANNEL_STABLE) ? "stable" : "dev");

    log(PSTR("[***Update Handling DEBUG] Free heap before: %u, max. block: %u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    WiFiClientSecure clientSecure;
    clientSecure.setSession(p_wifiSession);
    clientSecure.setTrustAnchors(p_wifiCertList);
    clientSecure.setBufferSizes(1024, 512);

    HTTPClient http;
    http.setTimeout(10000); // 10 Seconds
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    if(!http.begin(clientSecure, manifestUrl.c_str()))
    {
        log(PSTR("[Update Handling] HTTP error: Unable to connect to %s\n"), manifestUrl.c_str());
        return false;
    }
    log(PSTR("[***Update Handling DEBUG] Free heap after begin: %u, max. block: %u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    
    const char* headerKeys[] = 
    {
        "Content-Length",
        "Content-Type",
        "Location",
        "Server"
    };
    http.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
    
    int httpCode = http.GET();
    if (httpCode <= 0)
    {
        log(PSTR("[Update Handling] HTTP error: Code = %d, Message = %s\n"), httpCode, http.errorToString(httpCode).c_str());
    }

    if (httpCode != 200)
    {
        http.end();
        return false;
    }

    log(PSTR("[***Update Handling DEBUG] Size: %d\n"), http.getSize());
    log(PSTR("[***Update Handling DEBUG] Free heap after GET: %u, max. block: %u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    log(PSTR("[***Update Handling DEBUG] %d Headers received:\n"), http.headers());
    for (int i = 0; i < http.headers(); i++)
    {
        log(PSTR("[***Update Handling DEBUG] Header %d: %s = %s\n"), i, http.headerName(i).c_str(), http.header(i).c_str());
    }

    // Parse directly from stream to save stack and heap memory
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    
    log(PSTR("[***Update Handling DEBUG] Free heap after JSON: %u, max. block: %u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    http.end();

    log(PSTR("[***Update Handling DEBUG] Free heap after end: %u, max. block: %u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    clientSecure.stop();
    delay(100);
    log(PSTR("[***Update Handling DEBUG] after client.stop(): free=%u, max=%u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    if (err)
    {
        return false;
    }

    bool anyValid = false;
    for (size_t i = 0; i < this->getComponentCount(); ++i)
    {
        UpdateHandlingComponentBase* component = this->getComponentAt(i);
        if(component == nullptr) { continue; }
        update_info_t &info = component->updateInfo;
        const String componentName = component->componentName;
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
    for (size_t i = 0; i < this->getComponentCount(); ++i)
    {
        UpdateHandlingComponentBase* component = this->getComponentAt(i);
        if(component == nullptr) { continue; }
        update_info_t &info = component->updateInfo;
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
    doc["currentComponentName"] = status.currentComponent ? status.currentComponent->componentName : "";
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
            if (!jsonObj.containsKey("componentName") || !jsonObj.containsKey("componentInstanceIndex"))
            {
                request->send(400, "text/plain", "Missing 'componentName' or 'componentInstanceIndex' in JSON body");
                return;
            }
            String componentName = jsonObj["componentName"].as<String>();
            int componentInstanceIndex = jsonObj["componentInstanceIndex"].as<int>();

            StaticJsonDocument<128> doc;
            int resultCode = 200;
            UpdateHandlingComponentBase* component = this->getComponentByName(componentName);
            if (component != nullptr)
            {
                this->enqueueUpdateTasks(componentName, componentInstanceIndex);
                doc["status"] = "ok";
                doc["message"] = "Update for " + componentName + " started";
            }
            else
            {
                doc["status"] = "error";
                doc["message"] = "Invalid component \"" + componentName + "\"";
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

        for (size_t componentIndex = 0; componentIndex < this->getComponentCount(); ++componentIndex)
        {
            UpdateHandlingComponentBase* componentDef = this->getComponentAt(componentIndex);
            if(componentDef == nullptr) { continue; }
            update_info_t &info = componentDef->updateInfo;
            size_t instanceCount = componentDef->getInstanceCount();

            JsonObject component = componentsArray.createNestedObject();
            component["name"] = componentDef->componentName;
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
                versionsArray.add(componentDef->queryVersion(instanceIndex));
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
                obj["componentName"] = task.componentDef->componentName;
                obj["instance"] = task.componentInstanceIndex;
                obj["step"] = task.step;
            }
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    /*--------------------------------------------------------------------*/

    for (size_t componentIndex = 0; componentIndex < this->getComponentCount(); ++componentIndex)
    {
        UpdateHandlingComponentBase* component = this->getComponentAt(componentIndex);
        if (component)
        {
            component->initWebserverEndpoints(p_server);
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
    updateStatus.currentComponent = nullptr;
    // Set state to fetch the newest version infos in the next loop() iteration, because the HTTP request handling should be as fast as possible and not block for too long (e.g. by waiting for the HTTP response from the update server)
    updateStatus.state = UPDATE_STATE_CHECKING;
    return true;
}

/**********************************************************************/

bool UpdateHandling::enqueueSingleUpdateTask(String componentName, int instanceIndex, UpdateSteps step, update_step_handler_t handler, UpdateHandlingComponentBase* componentDef)
{
    update_task_t task;
    task.componentInstanceIndex = instanceIndex;
    task.step = step;
    task.handler = handler;
    task.componentDef = componentDef;
    return updateTaskQueue.push(task);
}


void UpdateHandling::enqueueUpdateTasks(String componentName, int componentInstanceIndex)
{
    UpdateHandlingComponentBase* componentDef = this->getComponentByName(componentName);
    if(componentDef != nullptr)
    {
        componentDef->enqueueUpdateTasks(componentInstanceIndex);
    }
    else
    {
        log(PSTR("[Update Handling] Cannot enqueue update tasks: Component \"%s\" not supported\n"), componentName.c_str());
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

            if(result)
            {
                log(PSTR("[Update Handling] Fetching version infos successful:"));
                for(size_t i = 0; i < this->getComponentCount(); ++i)
                {
                    UpdateHandlingComponentBase* component = this->getComponentAt(i);
                    if(component == nullptr) { continue; }
                    update_info_t &info = component->updateInfo;
                    log(PSTR("Component: %s\n"), component->componentName.c_str());
                    log(PSTR("  Valid: %s\n"), info.valid ? "true" : "false");
                    log(PSTR("  Version: %s\n"), info.version.c_str());
                    log(PSTR("  Has FS Update: %s\n"), info.has_fs_update ? "true" : "false");
                    log(PSTR("  URL FW: %s\n"), info.url_fw.c_str());
                    log(PSTR("  MD5 FW: %s\n"), info.fw_md5.c_str());
                    if(info.has_fs_update)
                    {
                        log(PSTR("  URL FS: %s\n"), info.url_fs.c_str());
                        log(PSTR("  MD5 FS: %s\n"), info.fs_md5.c_str());
                    }
                }
            }
            else
            {
                log(PSTR("[Update Handling] Fetching version infos failed"));
            }
            updateStatus.state = result ? UPDATE_STATE_IDLE : UPDATE_STATE_ERROR;
            break;
        }
        case UPDATE_STATE_UPDATING:
        {
            if(!updateTaskQueue.isEmpty())
            {
                updateTaskQueue.pop(currentUpdateTask);

                updateStatus.currentComponent = currentUpdateTask.componentDef;
                updateStatus.currentComponentInstanceIndex = currentUpdateTask.componentInstanceIndex;
                updateStatus.updateStep = currentUpdateTask.step;
                if(currentUpdateTask.handler == nullptr)
                {
                    log(PSTR("[Update Handling] No handler assigned"));
                    updateStatus.state = UPDATE_STATE_ERROR;
                }
                else
                {
                    log(PSTR("[Update Handling] Performing update task: Component = %s, Instance Index = %d, Step = %d\n"), currentUpdateTask.componentDef->componentName.c_str(), currentUpdateTask.componentInstanceIndex, currentUpdateTask.step);
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
            updateStatus.currentComponent = nullptr;
            updateStatus.currentComponentInstanceIndex = -1;
            updateStatus.updateStep = UPDATE_STEP_NONE;
            updateStatus.updateProgress = 0.0f;
            break;
        }
        default: break;
    }
}