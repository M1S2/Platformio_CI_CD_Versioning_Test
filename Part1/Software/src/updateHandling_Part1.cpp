#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "updateHandling.h"
#include "updateHandling_Part1.h"
#include "version.h"
#include "updateHandlingConfig.h"

/**********************************************************************/

update_info_t updateInfo_Part1;

// Flags used to synchronize with the web UI for backup/restore operations
volatile bool fsBackupConfirmed = false;
volatile bool fsRestoreConfirmed = false;

/**********************************************************************/

// Helper function: Check if filename matches a pattern (supports * wildcard)
bool filenameMatchesPattern(const char* filename, const char* pattern)
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

void updateHandling_Part1_initWebserverEndpoints(AsyncWebServer* p_server)
{
    // Filesystem backup/restore endpoints (used by the web UI to download/upload files)
    p_server->on("/fs/backup_file_list", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        StaticJsonDocument<2048> doc;
        JsonArray arr = doc.to<JsonArray>();
        
        const char* filesForBackup[] = UPDATE_BACKUP_FILES_ARRAY;
        size_t filesForBackupCount = sizeof(filesForBackup) / sizeof(filesForBackup[0]);
        // Check each pattern against files in LittleFS
        for (size_t i = 0; i < filesForBackupCount; i++)
        {
            const char* pattern = filesForBackup[i];
            
            // Check if pattern contains wildcard
            if (strchr(pattern, '*'))
            {
                // Wildcard pattern: iterate through all files in root directory and match
                Dir dir = LittleFS.openDir("/");
                while (dir.next())
                {
                    #ifdef DEBUG_OUTPUT
                        Serial.printf_P(PSTR("[Update Handling Part1] Checking file '%s' against pattern '%s'\n"), dir.fileName().c_str(), pattern);
                    #endif
                    String fileName = dir.fileName();
                    // Remove leading slash for matching
                    const char* fileNamePtr = fileName.c_str();
                    if (*fileNamePtr == '/') fileNamePtr++;
                    
                    if (filenameMatchesPattern(fileNamePtr, pattern))
                    {
                        #ifdef DEBUG_OUTPUT
                            Serial.printf_P(PSTR("[Update Handling Part1] Found file '%s'\n"), fileNamePtr);
                        #endif
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

    // Upload handler: client uploads files (used for restore). The onUpload lambda handles file chunks.
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

    // Confirmation endpoints: the web UI should call these after it finished backup/restore actions
    p_server->on("/fs/backup_confirm", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        fsBackupConfirmed = true;
        request->send(200, "text/plain", "OK");
    });

    p_server->on("/fs/restore_confirm", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        fsRestoreConfirmed = true;
        request->send(200, "text/plain", "OK");
    });
}

/**********************************************************************/

bool updateHandling_Part1_performUpdateTask_FS(update_task_t& updateTask)
{
    if (!updateInfo_Part1.valid || !updateInfo_Part1.has_fs_update)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] No valid update info available or no filesystem update included in the update"));
        #endif
        return false;
    }
    #ifdef DEBUG_OUTPUT
        Serial.printf_P(PSTR("[Update Handling Part1] Free heap before SSL: %u bytes\n"), ESP.getFreeHeap());
    #endif

    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);
    static int lastLoggedPercent = -1;

    ESPhttpUpdate.onStart([]()
    {
        lastLoggedPercent = -1;
        updateHandling.updateStatus.updateProgress = 0.0f;
    });
    ESPhttpUpdate.onEnd([]()
    {
        updateHandling.updateStatus.updateProgress = 100.0f;
    });
    ESPhttpUpdate.onProgress([](int cur, int total)
    {        
        float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
        int currentPercentInt = (int)percent;

        // Only process progress updates if the percentage has changed to reduce overhead and memory pressure
        if (currentPercentInt == lastLoggedPercent)
        {
            return;
        }
        lastLoggedPercent = currentPercentInt;

        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling Part1] Progress: %d / %d (%.2f%%)\n"), cur, total, percent);
        #endif
        updateHandling.updateStatus.updateProgress = percent;                
        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    WiFiClientSecure clientSecure;
    clientSecure.setSession(updateHandling.p_wifiSession);
    clientSecure.setTrustAnchors(updateHandling.p_wifiCertList);
    clientSecure.setBufferSizes(16384, 512);

    bool fsUpdateResult = true;
    #ifdef DEBUG_OUTPUT
        Serial.println(F("[Update Handling Part1] Update file system..."));
    #endif
    ESPhttpUpdate.setMD5sum(updateInfo_Part1.fs_md5);
    ESPhttpUpdate.rebootOnUpdate(false); // prevent automatic reboot so we can restore files afterwards
    t_httpUpdate_return returnFsUpdate = ESPhttpUpdate.updateFS(clientSecure, updateInfo_Part1.url_fs);
    switch (returnFsUpdate)
    {
        case HTTP_UPDATE_FAILED:
            #ifdef DEBUG_OUTPUT
                Serial.printf_P(PSTR("[Update Handling Part1] FS Update failed: %s\n"), ESPhttpUpdate.getLastErrorString().c_str());
            #endif
            break;
        case HTTP_UPDATE_NO_UPDATES:
            #ifdef DEBUG_OUTPUT    
                Serial.println(F("[Update Handling Part1] No FS update available"));
            #endif
            break;
        case HTTP_UPDATE_OK:
            #ifdef DEBUG_OUTPUT    
                Serial.println(F("[Update Handling Part1] FS Update ok"));
            #endif
            break;
    }
    fsUpdateResult = (returnFsUpdate == HTTP_UPDATE_OK);
    return fsUpdateResult;
}

/**********************************************************************/

bool updateHandling_Part1_performUpdateTask_FW(update_task_t& updateTask)
{
    if (!updateInfo_Part1.valid)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] No valid update info available"));
        #endif
        return false;
    }

    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);
    static int lastLoggedPercent = -1;

    ESPhttpUpdate.onStart([]()
    {
        lastLoggedPercent = -1;
        updateHandling.updateStatus.updateProgress = 0.0f;
    });
    ESPhttpUpdate.onEnd([]()
    {
        updateHandling.updateStatus.updateProgress = 100.0f;
    });
    ESPhttpUpdate.onProgress([](int cur, int total)
    {        
        float percent = (total > 0) ? (100.0f * cur / total) : 0.0f;
        int currentPercentInt = (int)percent;

        // Only process progress updates if the percentage has changed to reduce overhead and memory pressure
        if (currentPercentInt == lastLoggedPercent)
        {
            return;
        }
        lastLoggedPercent = currentPercentInt;

        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling Part1] Progress: %d / %d (%.2f%%)\n"), cur, total, percent);
        #endif
        updateHandling.updateStatus.updateProgress = percent;                
        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    WiFiClientSecure clientSecure;
    clientSecure.setSession(updateHandling.p_wifiSession);
    clientSecure.setTrustAnchors(updateHandling.p_wifiCertList);
    clientSecure.setBufferSizes(16384, 512);

    bool fwUpdateResult = true;
    #ifdef DEBUG_OUTPUT
        Serial.println(F("[Update Handling Part1] Update firmware..."));
    #endif
    ESPhttpUpdate.setMD5sum(updateInfo_Part1.fw_md5);
    ESPhttpUpdate.rebootOnUpdate(false);    // Don't reboot automatically after the firmware update.
    t_httpUpdate_return returnFwUpdate = ESPhttpUpdate.update(clientSecure, updateInfo_Part1.url_fw);
    switch (returnFwUpdate)
    {
        case HTTP_UPDATE_FAILED:
            #ifdef DEBUG_OUTPUT
                Serial.printf_P(PSTR("[Update Handling Part1] Update failed: %s\n"), ESPhttpUpdate.getLastErrorString().c_str());
            #endif
            break;
        case HTTP_UPDATE_NO_UPDATES:
            #ifdef DEBUG_OUTPUT    
                Serial.println(F("[Update Handling Part1] No update available"));
            #endif
            break;
        case HTTP_UPDATE_OK:
            #ifdef DEBUG_OUTPUT    
                Serial.println(F("[Update Handling Part1] Update ok"));
            #endif
            break;
    }
    fwUpdateResult = (returnFwUpdate == HTTP_UPDATE_OK);
    return fwUpdateResult;
}

/**********************************************************************/

bool updateHandling_Part1_performUpdateTask_RESTART(update_task_t& updateTask)
{
    updateHandling.updateStatus.state = UPDATE_STATE_RESTARTING;

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

bool updateHandling_Part1_performUpdateTask_BACKUP(update_task_t& updateTask)
{
    // If there is no FS update, nothing to backup
    if (!updateInfo_Part1.valid || !updateInfo_Part1.has_fs_update)
    {
        return true;
    }

    // Wait for confirmation
    unsigned long start = millis();
    while (!fsBackupConfirmed && (UPDATE_PART1BACKUPRESTORE_TIMEOUT_MS == 0 || (millis() - start) < UPDATE_PART1BACKUPRESTORE_TIMEOUT_MS))
    {
        yield();
    }

    if (!fsBackupConfirmed)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] FS backup confirmation timed out"));
        #endif
        return false;
    }
    return true;
}

/**********************************************************************/

bool updateHandling_Part1_performUpdateTask_RESTORE(update_task_t& updateTask)
{
    // If there is no FS update, nothing to restore
    if (!updateInfo_Part1.valid || !updateInfo_Part1.has_fs_update)
    {
        return true;
    }

    // Wait for confirmation
    unsigned long start = millis();
    while (!fsRestoreConfirmed && (UPDATE_PART1BACKUPRESTORE_TIMEOUT_MS == 0 || (millis() - start) < UPDATE_PART1BACKUPRESTORE_TIMEOUT_MS))
    {
        yield();
    }

    if (!fsRestoreConfirmed)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] FS restore confirmation timed out"));
        #endif
        return false;
    }
    return true;
}

/**********************************************************************/

bool updateHandling_Part1_enqueueUpdateTasks(int componentInstanceIndex = -1)
{
    // If a filesystem update is available, enqueue backup -> fs update -> restore
    if (updateInfo_Part1.valid && updateInfo_Part1.has_fs_update)
    {
        // Reset confirmation flags
        fsBackupConfirmed = false;
        fsRestoreConfirmed = false;

        if(!updateHandling.enqueueSingleUpdateTask(UPDATE_COMPONENT_PART1, componentInstanceIndex, UPDATE_STEP_BACKUP, updateHandling_Part1_performUpdateTask_BACKUP))
        {
            return false;
        }
        if(!updateHandling.enqueueSingleUpdateTask(UPDATE_COMPONENT_PART1, componentInstanceIndex, UPDATE_STEP_FS, updateHandling_Part1_performUpdateTask_FS))
        {
            return false;
        }
        if(!updateHandling.enqueueSingleUpdateTask(UPDATE_COMPONENT_PART1, componentInstanceIndex, UPDATE_STEP_RESTORE, updateHandling_Part1_performUpdateTask_RESTORE))
        {
            return false;
        }
    }

    if(!updateHandling.enqueueSingleUpdateTask(UPDATE_COMPONENT_PART1, componentInstanceIndex, UPDATE_STEP_FW, updateHandling_Part1_performUpdateTask_FW))
    {
        return false;
    }
    if(!updateHandling.enqueueSingleUpdateTask(UPDATE_COMPONENT_PART1, componentInstanceIndex, UPDATE_STEP_RESTART, updateHandling_Part1_performUpdateTask_RESTART))
    {
        return false;
    }
    return true;
}


/**********************************************************************/

size_t updateHandling_Part1_getInstanceCount()
{
    // There is only one instance of Part1, so we can return 1
    return 1;
}

/**********************************************************************/

char* updateHandling_Part1_queryVersion(int componentInstanceIndex = -1)
{
    // There is only one instance of Part1, so we can ignore the componentInstanceIndex parameter
    return FW_VERSION;
}