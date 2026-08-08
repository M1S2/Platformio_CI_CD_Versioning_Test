#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MD5Builder.h>
#include "updateHandling.h"
#include "updateHandling_Remote.h"
#include "remoteActionHub.h"
#include "config.h"

/**********************************************************************/

#define LITTLEFS_REMOTE_FW_PATH "/fw/remote_fw.bin"

UpdateHandlingRemote::UpdateHandlingRemote() : UpdateHandlingComponentBase(UPDATE_COMPONENTNAME_REMOTE)
{
}

bool UpdateHandlingRemote::enqueueUpdateTasks(int componentInstanceIndex)
{
    if(!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_REMOTE, componentInstanceIndex, UPDATE_STEP_PREPARE, UpdateHandlingRemote::performUpdateTask_PREPARE, this))
    {
        return false;
    }
    if(!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_REMOTE, componentInstanceIndex, UPDATE_STEP_WAIT, UpdateHandlingRemote::performUpdateTask_WAIT, this))
    {
        return false;
    }
    if(!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_REMOTE, componentInstanceIndex, UPDATE_STEP_FW, UpdateHandlingRemote::performUpdateTask_FW, this))
    {
        return false;
    }
    return true;
}

size_t UpdateHandlingRemote::getInstanceCount()
{
    #warning At the moment there are two instances of Remote. Make this variable.
    return 2;
}

char* UpdateHandlingRemote::queryVersion(int componentInstanceIndex)
{
    switch (componentInstanceIndex)
    {
        case 0: return "?";
        case 1: return "?";
        default: return "?";
    }
    return "?"; // Default case, should not be reached
}

void UpdateHandlingRemote::initWebserverEndpoints(AsyncWebServer* p_server)
{
    p_server->on("/update/remote_fw.bin", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        if(!remoteActionHub_isAPOpen)
        {
            request->send(404, "text/plain", "Action hub not open");
            return;
        }
        connectionEstablished = true;
        if(!LittleFS.exists(LITTLEFS_REMOTE_FW_PATH))
        {
            request->send(404, "text/plain", "File not found");
            return;
        }
        p_updateHandling->updateStatus.updateStep = UPDATE_STEP_FW;
        request->send(LittleFS, LITTLEFS_REMOTE_FW_PATH, "application/octet-stream");
    });
    
    /*--------------------------------------------------------------------*/

    p_server->on("/update/remote_fw_md5", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        connectionEstablished = true;
        if(!updateInfo.valid)
        {
            request->send(404, "text/plain", "No valid update info available");
            return;
        }
        request->send(200, "text/plain", updateInfo.fw_md5);
    });
    
    /*--------------------------------------------------------------------*/
    
    p_server->on("/update/remote_report", HTTP_POST, [this](AsyncWebServerRequest *request)
    {
        if (request->hasParam("progress", true))
        {
            p_updateHandling->updateStatus.updateProgress = request->getParam("progress", true)->value().toFloat();
        }
        if (request->hasParam("finished", true) && request->getParam("finished", true)->value() == "true")
        {
            fwUpdateFinished = true;
            p_updateHandling->updateStatus.updateStep = UPDATE_STEP_FINISHED;
        }
        request->send(200, "text/plain", "OK");
    });
}

/**********************************************************************/

// Calculate the MD5 hash of a file in the LittleFS
String UpdateHandlingRemote::calculateFileMD5(const String &filePath, UpdateHandling* p_updateHandling)
{
    File file = LittleFS.open(filePath, "r");
    if (!file)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] Failed to open file %s for MD5 calculation\n"), filePath.c_str());
        return "";
    }

    MD5Builder md5;
    md5.begin();
    md5.addStream(file, file.size());
    md5.calculate();
    file.close();
    return md5.toString();
}

/**********************************************************************/

bool UpdateHandlingRemote::downloadFileToLittleFS(const String &url, const String &filePath, const String &expectedMd5, update_task_t& updateTask)
{
    UpdateHandling* p_updateHandling = updateTask.componentDef->p_updateHandling;
 
    p_updateHandling->log(PSTR("[Update Handling Remote] Downloading file..."));

    // Check, if the file already exists and the MD5 hash matches
    if (LittleFS.exists(filePath))
    {
        String currentMd5 = calculateFileMD5(filePath, p_updateHandling);
        if (currentMd5 == expectedMd5)
        {
            p_updateHandling->log(PSTR("[Update Handling Remote] File %s already exists and MD5 matches. Skipping download.\n"), filePath.c_str());
            return true;
        }
        else
        {
            p_updateHandling->log(PSTR("[Update Handling Remote] File %s exists but MD5 mismatch (expected: %s, actual: %s). Deleting and re-downloading.\n"), filePath.c_str(), expectedMd5.c_str(), currentMd5.c_str());
            LittleFS.remove(filePath); // Remove old file
        }
    }

    WiFiClientSecure clientSecure;
    clientSecure.setSession(p_updateHandling->p_wifiSession);
    clientSecure.setTrustAnchors(p_updateHandling->p_wifiCertList);
    clientSecure.setBufferSizes(16384, 512);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setReuse(false);
    if (!http.begin(clientSecure, url))
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] http.begin failed"));
        http.end();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] HTTP error: Code = %d, Message = %s\n"), httpCode, http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    File file = LittleFS.open(filePath, "w");
    if (!file)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] Failed to open file"));
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] Failed to get stream pointer"));
        file.close();
        http.end();
        return false;
    }
    p_updateHandling->log(PSTR("[Update Handling Remote] Stream successfully opened, %d bytes to download\n"), contentLength);

    // Use stack buffer to avoid heap fragmentation during SSL download
    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];

    uint32_t totalWritten = 0;

    static int lastLoggedPercent = -1;
    lastLoggedPercent = -1;

    while (http.connected() || (stream && stream->available()))
    {
        if (contentLength > 0 && totalWritten >= (uint32_t)contentLength)
        {
            break;
        }

        size_t available = stream->available();
        if (available)
        {
            size_t len = stream->readBytes(buffer, min(available, bufferSize));
            file.write(buffer, len);
            totalWritten += len;

            float percent = (contentLength > 0) ? (100.0f * totalWritten / contentLength) : 0.0f;
            int currentPercentInt = (int)percent;
            
            // Only update status and log if percent changed to save memory pressure
            if (currentPercentInt != lastLoggedPercent)
            {
                lastLoggedPercent = currentPercentInt;
                p_updateHandling->updateStatus.updateProgress = percent;
                p_updateHandling->log(PSTR("[Update Handling Remote] Downloaded: %u bytes  -> %.2f%%\n"), totalWritten, percent);
            }
        }
        yield();
    }

    file.close();
    http.end();

    // Check if the download was actually complete
    if (contentLength > 0 && totalWritten < (uint32_t)contentLength)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] Download failed: Interrupted at %u of %d bytes\n"), totalWritten, contentLength);
        return false;
    }

    // Check MD5 hash after download
    String downloadedMd5 = calculateFileMD5(filePath, p_updateHandling);
    if (downloadedMd5 != expectedMd5)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] Downloaded file MD5 mismatch! Expected: %s, Actual: %s. Deleting file.\n"), expectedMd5.c_str(), downloadedMd5.c_str());
        LittleFS.remove(filePath); // Defect file -> delete it
        return false;
    }

    p_updateHandling->log(PSTR("[Update Handling Remote] Download finished and MD5 verified.\n"));
    return true;
}

/**********************************************************************/

bool UpdateHandlingRemote::performUpdateTask_PREPARE(update_task_t& updateTask)
{
    UpdateHandlingComponentBase* p_componentDef = updateTask.componentDef;
    UpdateHandling* p_updateHandling = p_componentDef->p_updateHandling;

    if (!p_componentDef->updateInfo.valid)
    {
        p_updateHandling->log(PSTR("[Update Handling Remote] No valid update info available"));
        return false;
    }
    return downloadFileToLittleFS(p_componentDef->updateInfo.url_fw, LITTLEFS_REMOTE_FW_PATH, p_componentDef->updateInfo.fw_md5, updateTask);
}

/**********************************************************************/

bool UpdateHandlingRemote::performUpdateTask_WAIT(update_task_t& updateTask)
{
    UpdateHandlingRemote* p_component = (UpdateHandlingRemote*)updateTask.componentDef;
    
    p_component->connectionEstablished = false;
    p_component->fwUpdateFinished = false;
    if(!remoteActionHub_startAP(REMOTEACTIONHUB_ACTION_UPDATE)) { return false; }

    while(!p_component->connectionEstablished)
    {
        if(remoteActionHub_handleAPTimeout())
        {
            // The action hub was closed by timeout.
            break;
        }
        yield();
    }
    return p_component->connectionEstablished;
}

/**********************************************************************/

bool UpdateHandlingRemote::performUpdateTask_FW(update_task_t& updateTask)
{
    UpdateHandlingRemote* p_component = (UpdateHandlingRemote*)updateTask.componentDef;
    while(!p_component->fwUpdateFinished)
    {
        if(remoteActionHub_handleAPTimeout())
        {
            // The action hub was closed by timeout.
            break;
        }
        yield();
    }

    remoteActionHub_stopAP();
    // Cleanup downloaded file
    if(LittleFS.exists(LITTLEFS_REMOTE_FW_PATH))
    {
        LittleFS.remove(LITTLEFS_REMOTE_FW_PATH);
    }
    return p_component->fwUpdateFinished;
}