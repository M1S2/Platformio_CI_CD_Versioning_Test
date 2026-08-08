#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "updateHandling.h"
#include "updateHandling_Part1.h"
#include "version.h"
#include "config.h"

/**********************************************************************/

// Static member initialization
unsigned long UpdateHandlingPart1::backupRestoreTimeoutMs = 0;

UpdateHandlingPart1::UpdateHandlingPart1(unsigned long backupRestoreTimeoutMs, const char** filesForBackup, size_t filesForBackupCount) : UpdateHandlingComponentBase(UPDATE_COMPONENTNAME_PART1)
{
    UpdateHandlingPart1::backupRestoreTimeoutMs = backupRestoreTimeoutMs;
    this->filesForBackup = filesForBackup;
    this->filesForBackupCount = filesForBackupCount;
}

bool UpdateHandlingPart1::enqueueUpdateTasks(int componentInstanceIndex)
{
    if (updateInfo.valid && updateInfo.has_fs_update)
    {
        fsBackupConfirmed = false;
        fsRestoreConfirmed = false;

        if (!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_PART1, componentInstanceIndex, UPDATE_STEP_BACKUP, UpdateHandlingPart1::performUpdateTask_BACKUP, this))
        {
            return false;
        }
        if (!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_PART1, componentInstanceIndex, UPDATE_STEP_FS, UpdateHandlingPart1::performUpdateTask_FS, this))
        {
            return false;
        }
        if (!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_PART1, componentInstanceIndex, UPDATE_STEP_RESTORE, UpdateHandlingPart1::performUpdateTask_RESTORE, this))
        {
            return false;
        }
    }

    if (!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_PART1, componentInstanceIndex, UPDATE_STEP_FW, UpdateHandlingPart1::performUpdateTask_FW, this))
    {
        return false;
    }
    if (!p_updateHandling->enqueueSingleUpdateTask(UPDATE_COMPONENTNAME_PART1, componentInstanceIndex, UPDATE_STEP_RESTART, UpdateHandlingPart1::performUpdateTask_RESTART, this))
    {
        return false;
    }
    return true;
}

size_t UpdateHandlingPart1::getInstanceCount()
{
    return 1;
}

char* UpdateHandlingPart1::queryVersion(int componentInstanceIndex)
{
    return FW_VERSION;
}

void UpdateHandlingPart1::initWebserverEndpoints(AsyncWebServer* p_server)
{
    // Filesystem backup/restore endpoints (used by the web UI to download/upload files)
    p_server->on("/fs/backup_file_list", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        StaticJsonDocument<2048> doc;
        JsonArray arr = doc.to<JsonArray>();
        
        // Check each pattern against files in LittleFS
        for (size_t i = 0; i < filesForBackupCount; i++)
        {
            const char* pattern = this->filesForBackup[i];
            
            // Check if pattern contains wildcard
            if (strchr(pattern, '*'))
            {
                // Wildcard pattern: iterate through all files in root directory and match
                Dir dir = LittleFS.openDir("/");
                while (dir.next())
                {
                    p_updateHandling->log(PSTR("[Update Handling Part1] Checking file '%s' against pattern '%s'\n"), dir.fileName().c_str(), pattern);
                    String fileName = dir.fileName();
                    // Remove leading slash for matching
                    const char* fileNamePtr = fileName.c_str();
                    if (*fileNamePtr == '/') fileNamePtr++;
                    
                    if (filenameMatchesPattern(fileNamePtr, pattern))
                    {
                        p_updateHandling->log(PSTR("[Update Handling Part1] Found file '%s'\n"), fileNamePtr);
                        // Create a safe copy into the JsonDocument by passing a String
                        arr.add(String(fileNamePtr));
                    }
                }
            }
            else
            {
                // Fixed path: just check if it exists
                String path = "/" + String(pattern);
                if (LittleFS.exists(path))
                {
                    // Create a safe copy into the JsonDocument by passing a String
                    arr.add(String(pattern));
                }
            }
        }
        
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    p_server->on("/fs/download", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if (!request->hasParam("file"))
        {
            request->send(400, "text/plain", "Missing 'file' parameter");
            return;
        }
        String fn = request->getParam("file")->value();
        String path = "/" + fn;
        if (!LittleFS.exists(path))
        {
            request->send(404, "text/plain", "Not found");
            return;
        }
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "application/octet-stream");
        request->send(response);
    });

    p_server->on("/fs/upload", HTTP_POST, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", "OK"); },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
    {
        static File uploadFile;
        String path = "/" + filename;
        if (index == 0)
        {
            if (LittleFS.exists(path)) LittleFS.remove(path);
            uploadFile = LittleFS.open(path, "w");
        }
        if (len && uploadFile) uploadFile.write(data, len);
        if (final && uploadFile) uploadFile.close();
    });

    p_server->on("/fs/backup_confirm", [this](AsyncWebServerRequest *request)
    {
        fsBackupConfirmed = true;
        request->send(200, "text/plain", "OK");
    });

    p_server->on("/fs/restore_confirm", [this](AsyncWebServerRequest *request)
    {
        fsRestoreConfirmed = true;
        request->send(200, "text/plain", "OK");
    });
}

/**********************************************************************/

// Helper function: Check if filename matches a pattern (supports * wildcard)
bool UpdateHandlingPart1::filenameMatchesPattern(const char* filename, const char* pattern)
{
    while (*pattern)
    {
        if (*pattern == '*')
        {
            // If * is at the end of pattern, it matches everything remaining
            if (*(pattern + 1) == '\0')
                return true;
            
            // Find the next non-wildcard character in pattern
            pattern++;
            while (*filename && *filename != *pattern)
                filename++;
            
            // Skip wildcard in pattern
            if (!*filename && *pattern)
                return false;
        }
        else if (*filename == *pattern)
        {
            filename++;
            pattern++;
        }
        else
        {
            return false;
        }
    }
    
    return *filename == '\0';
}

/**********************************************************************/

bool UpdateHandlingPart1::performUpdateTask_FS(update_task_t& updateTask)
{
    UpdateHandlingComponentBase* p_componentDef = updateTask.componentDef;
    UpdateHandling* p_updateHandling = p_componentDef->p_updateHandling;

    if (!p_componentDef->updateInfo.valid || !p_componentDef->updateInfo.has_fs_update)
    {
        p_updateHandling->log(PSTR("[Update Handling Part1] No valid update info available or no filesystem update included in the update"));
        return false;
    }
    p_updateHandling->log(PSTR("[Update Handling Part1] Free heap before SSL: %u bytes\n"), ESP.getFreeHeap());

    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);
    static int lastLoggedPercent = -1;

    ESPhttpUpdate.onStart([p_updateHandling]()
    {
        lastLoggedPercent = -1;
        p_updateHandling->updateStatus.updateProgress = 0.0f;
    });
    ESPhttpUpdate.onEnd([p_updateHandling]()
    {
        p_updateHandling->updateStatus.updateProgress = 100.0f;
    });
    ESPhttpUpdate.onProgress([p_updateHandling](int cur, int total)
    {        
        float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
        int currentPercentInt = (int)percent;

        // Only process progress updates if the percentage has changed to reduce overhead and memory pressure
        if (currentPercentInt == lastLoggedPercent)
        {
            return;
        }
        lastLoggedPercent = currentPercentInt;

        p_updateHandling->log(PSTR("[Update Handling Part1] Progress: %d / %d (%.2f%%)\n"), cur, total, percent);
        p_updateHandling->updateStatus.updateProgress = percent;                
        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    WiFiClientSecure clientSecure;
    clientSecure.setSession(p_updateHandling->p_wifiSession);
    clientSecure.setTrustAnchors(p_updateHandling->p_wifiCertList);
    clientSecure.setBufferSizes(16384, 512);

    bool fsUpdateResult = true;
    p_updateHandling->log(PSTR("[Update Handling Part1] Update file system..."));
    ESPhttpUpdate.setMD5sum(p_componentDef->updateInfo.fs_md5);
    ESPhttpUpdate.rebootOnUpdate(false); // prevent automatic reboot so we can restore files afterwards
    t_httpUpdate_return returnFsUpdate = ESPhttpUpdate.updateFS(clientSecure, p_componentDef->updateInfo.url_fs);
    switch (returnFsUpdate)
    {
        case HTTP_UPDATE_FAILED:
            p_updateHandling->log(PSTR("[Update Handling Part1] FS Update failed: %s\n"), ESPhttpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            p_updateHandling->log(PSTR("[Update Handling Part1] No FS update available"));
            break;
        case HTTP_UPDATE_OK:
            p_updateHandling->log(PSTR("[Update Handling Part1] FS Update ok"));
            break;
    }
    fsUpdateResult = (returnFsUpdate == HTTP_UPDATE_OK);
    return fsUpdateResult;
}

/**********************************************************************/

bool UpdateHandlingPart1::performUpdateTask_FW(update_task_t& updateTask)
{
    UpdateHandlingComponentBase* p_componentDef = updateTask.componentDef;
    UpdateHandling* p_updateHandling = p_componentDef->p_updateHandling;

    if (!p_componentDef->updateInfo.valid)
    {
        p_updateHandling->log(PSTR("[Update Handling Part1] No valid update info available"));
        return false;
    }
    
    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);
    static int lastLoggedPercent = -1;

    ESPhttpUpdate.onStart([p_updateHandling]()
    {
        lastLoggedPercent = -1;
        p_updateHandling->updateStatus.updateProgress = 0.0f;
    });
    ESPhttpUpdate.onEnd([p_updateHandling]()
    {
        p_updateHandling->updateStatus.updateProgress = 100.0f;
    });
    ESPhttpUpdate.onProgress([p_updateHandling](int cur, int total)
    {        
        float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
        int currentPercentInt = (int)percent;

        // Only process progress updates if the percentage has changed to reduce overhead and memory pressure
        if (currentPercentInt == lastLoggedPercent)
        {
            return;
        }
        lastLoggedPercent = currentPercentInt;

        p_updateHandling->log(PSTR("[Update Handling Part1] Progress: %d / %d (%.2f%%)\n"), cur, total, percent);
        p_updateHandling->updateStatus.updateProgress = percent;                
        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    WiFiClientSecure clientSecure;
    clientSecure.setSession(p_updateHandling->p_wifiSession);
    clientSecure.setTrustAnchors(p_updateHandling->p_wifiCertList);
    clientSecure.setBufferSizes(16384, 512);

    bool fwUpdateResult = true;
    p_updateHandling->log(PSTR("[Update Handling Part1] Update firmware..."));
    ESPhttpUpdate.setMD5sum(p_componentDef->updateInfo.fw_md5);
    ESPhttpUpdate.rebootOnUpdate(false);    // Don't reboot automatically after the firmware update.
    t_httpUpdate_return returnFwUpdate = ESPhttpUpdate.update(clientSecure, p_componentDef->updateInfo.url_fw);
    switch (returnFwUpdate)
    {
        case HTTP_UPDATE_FAILED:
            p_updateHandling->log(PSTR("[Update Handling Part1] Update failed: %s\n"), ESPhttpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            p_updateHandling->log(PSTR("[Update Handling Part1] No update available"));
            break;
        case HTTP_UPDATE_OK:
            p_updateHandling->log(PSTR("[Update Handling Part1] Update ok"));
            break;
    }
    fwUpdateResult = (returnFwUpdate == HTTP_UPDATE_OK);
    return fwUpdateResult;
}

/**********************************************************************/

bool UpdateHandlingPart1::performUpdateTask_RESTART(update_task_t& updateTask)
{
    UpdateHandling* p_updateHandling = updateTask.componentDef->p_updateHandling;
    
    p_updateHandling->updateStatus.state = UPDATE_STATE_RESTARTING;

    // Wait for some seconds to ensure that the HTTP response is sent completely before restarting.
    // This is especially important if the update was triggered via the web interface, because otherwise the web interface might not receive the response and thus not know that the update was successful.
    unsigned long start = millis();
    while (millis() - start < 3000)
    {
        yield();
    }
    ESP.restart();
    return true;
}

/**********************************************************************/

bool UpdateHandlingPart1::performUpdateTask_BACKUP(update_task_t& updateTask)
{
    UpdateHandlingPart1* p_componentPart1 = (UpdateHandlingPart1*)updateTask.componentDef;
    UpdateHandling* p_updateHandling = p_componentPart1->p_updateHandling;
    
    // If there is no FS update, nothing to backup
    if (!p_componentPart1->updateInfo.valid || !p_componentPart1->updateInfo.has_fs_update)
    {
        return true;
    }

    // Wait for confirmation
    unsigned long start = millis();
    while (!p_componentPart1->fsBackupConfirmed && (UpdateHandlingPart1::backupRestoreTimeoutMs == 0 || (millis() - start) < UpdateHandlingPart1::backupRestoreTimeoutMs))
    {
        yield();
    }

    if (!p_componentPart1->fsBackupConfirmed)
    {
        p_updateHandling->log(PSTR("[Update Handling Part1] FS backup confirmation timed out"));
        return false;
    }
    return true;
}

/**********************************************************************/

bool UpdateHandlingPart1::performUpdateTask_RESTORE(update_task_t& updateTask)
{
    UpdateHandlingPart1* p_componentPart1 = (UpdateHandlingPart1*)updateTask.componentDef;
    UpdateHandling* p_updateHandling = p_componentPart1->p_updateHandling;

    // If there is no FS update, nothing to restore
    if (!p_componentPart1->updateInfo.valid || !p_componentPart1->updateInfo.has_fs_update)
    {
        return true;
    }

    // Wait for confirmation
    unsigned long start = millis();
    while (!p_componentPart1->fsRestoreConfirmed && (UpdateHandlingPart1::backupRestoreTimeoutMs == 0 || (millis() - start) < UpdateHandlingPart1::backupRestoreTimeoutMs))
    {
        yield();
    }

    if (!p_componentPart1->fsRestoreConfirmed)
    {
        p_updateHandling->log(PSTR("[Update Handling Part1] FS restore confirmation timed out"));
        return false;
    }
    return true;
}