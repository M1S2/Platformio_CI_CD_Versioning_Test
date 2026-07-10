#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MD5Builder.h>
#include "updateHandling.h"
#include "updateHandling_Part2.h"
#include "timeHandling.h"
#include "wifiHandling.h"
#include "part2ActionHub.h"

/**********************************************************************/

update_info_t updateInfo_Part2;

#define LITTLEFS_PART2_FW_PATH "/fw/part2_fw.bin"

bool connectionEstablished = false;
bool fwUpdateFinished = false;

/**********************************************************************/

// Calculate the MD5 hash of a file in the LittleFS
String calculateFileMD5(const String &filePath)
{
    File file = LittleFS.open(filePath, "r");
    if (!file)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling Part2] Failed to open file %s for MD5 calculation\n"), filePath.c_str());
        #endif
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

bool updateHandling_downloadFileToLittleFS(const String &url, const String &filePath, const String &expectedMd5)
{
    #ifdef DEBUG_OUTPUT
        Serial.println(F("[Update Handling Part2] Downloading file..."));
    #endif

    // Check, if the file already exists and the MD5 hash matches
    if (LittleFS.exists(filePath))
    {
        String currentMd5 = calculateFileMD5(filePath);
        if (currentMd5 == expectedMd5)
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf_P(PSTR("[Update Handling Part2] File %s already exists and MD5 matches. Skipping download.\n"), filePath.c_str());
            #endif
            return true;
        }
        else
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf_P(PSTR("[Update Handling Part2] File %s exists but MD5 mismatch (expected: %s, actual: %s). Deleting and re-downloading.\n"), filePath.c_str(), expectedMd5.c_str(), currentMd5.c_str());
            #endif
            LittleFS.remove(filePath); // Remove old file
        }
    }

    WiFiClientSecure clientSecure;
    clientSecure.setSession(&session);
    clientSecure.setTrustAnchors(&certList);
    clientSecure.setBufferSizes(16384, 512);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setReuse(false);
    if (!http.begin(clientSecure, url))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part2] http.begin failed"));
        #endif
        http.end();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling Part2] HTTP error: Code = %d, Message = %s\n"), httpCode, http.errorToString(httpCode).c_str());
        #endif
        http.end();
        return false;
    }

    File file = LittleFS.open(filePath, "w");
    if (!file)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part2] Failed to open file"));
        #endif
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part2] Failed to get stream pointer"));
        #endif
        file.close();
        http.end();
        return false;
    }
    #ifdef DEBUG_OUTPUT
        Serial.printf_P(PSTR("[Update Handling Part2] Stream successfully opened, %d bytes to download\n"), contentLength);
    #endif

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
                updateStatus.updateProgress = percent;
                #ifdef DEBUG_OUTPUT
                    Serial.printf_P(PSTR("[Update Handling Part2] Downloaded: %u bytes  -> %.2f%%\n"), totalWritten, percent);
                #endif
            }
        }
        yield();
    }

    file.close();
    http.end();

    // Check if the download was actually complete
    if (contentLength > 0 && totalWritten < (uint32_t)contentLength)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling Part2] Download failed: Interrupted at %u of %d bytes\n"), totalWritten, contentLength);
        #endif
        return false;
    }

    // Check MD5 hash after download
    String downloadedMd5 = calculateFileMD5(filePath);
    if (downloadedMd5 != expectedMd5)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf_P(PSTR("[Update Handling Part2] Downloaded file MD5 mismatch! Expected: %s, Actual: %s. Deleting file.\n"), expectedMd5.c_str(), downloadedMd5.c_str());
        #endif
        LittleFS.remove(filePath); // Defect file -> delete it
        return false;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println(F("[Update Handling Part2] Download finished and MD5 verified."));
    #endif
    return true;
}

/**********************************************************************/

void updateHandling_Part2_initWebserverEndpoints()
{
    server.on("/update/part2_fw.bin", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if(!part2ActionHub_isAPOpen)
        {
            request->send(404, "text/plain", "Action hub not open");
            return;
        }
        connectionEstablished = true;
        if(!LittleFS.exists(LITTLEFS_PART2_FW_PATH))
        {
            request->send(404, "text/plain", "File not found");
            return;
        }
        updateStatus.updateStep = UPDATE_STEP_FW;
        request->send(LittleFS, LITTLEFS_PART2_FW_PATH, "application/octet-stream");
    });
    
    /*--------------------------------------------------------------------*/

    server.on("/update/part2_fw_md5", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        connectionEstablished = true;
        if(!updateInfo_Part2.valid)
        {
            request->send(404, "text/plain", "No valid update info available");
            return;
        }
        request->send(200, "text/plain", updateInfo_Part2.fw_md5);
    });
    
    /*--------------------------------------------------------------------*/
    
    server.on("/update/part2_report", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        if (request->hasParam("progress", true))
        {
            updateStatus.updateProgress = request->getParam("progress", true)->value().toFloat();
        }
        if (request->hasParam("finished", true) && request->getParam("finished", true)->value() == "true")
        {
            fwUpdateFinished = true;
            updateStatus.updateStep = UPDATE_STEP_FINISHED;
        }
        request->send(200, "text/plain", "OK");
    });
}

/**********************************************************************/

bool updateHandling_Part2_performUpdateTask_PREPARE(update_task_t& updateTask)
{
    if (!updateInfo_Part2.valid)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part2] No valid update info available"));
        #endif
        return false;
    }
    if(isTimeValid == false)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part2] Time is not valid yet, cannot check for updates because SSL certificate validation will fail. Try again later..."));
        #endif
        return false;
    }
    return updateHandling_downloadFileToLittleFS(updateInfo_Part2.url_fw, LITTLEFS_PART2_FW_PATH, updateInfo_Part2.fw_md5);
}

/**********************************************************************/

bool updateHandling_Part2_performUpdateTask_WAIT(update_task_t& updateTask)
{
    connectionEstablished = false;
    fwUpdateFinished = false;
    if(!part2ActionHub_startAP(PART2ACTIONHUB_ACTION_UPDATE)) { return false; }

    while(!connectionEstablished)
    {
        if(part2ActionHub_handleAPTimeout())
        {
            // The action hub was closed by timeout.
            break;
        }
        yield();
    }
    return connectionEstablished;
}

/**********************************************************************/

bool updateHandling_Part2_performUpdateTask_FW(update_task_t& updateTask)
{
    while(!fwUpdateFinished)
    {
        if(part2ActionHub_handleAPTimeout())
        {
            // The action hub was closed by timeout.
            break;
        }
        yield();
    }

    part2ActionHub_stopAP();
    // Cleanup downloaded file
    if(LittleFS.exists(LITTLEFS_PART2_FW_PATH))
    {
        LittleFS.remove(LITTLEFS_PART2_FW_PATH);
    }
    return fwUpdateFinished;
}

/**********************************************************************/

bool updateHandling_Part2_enqueueUpdateTasks(int componentInstanceIndex = -1)
{
    if(!updateHandling_enqueueSingleUpdateTask(UPDATE_COMPONENT_PART2, componentInstanceIndex, UPDATE_STEP_PREPARE, updateHandling_Part2_performUpdateTask_PREPARE))
    {
        return false;
    }
    if(!updateHandling_enqueueSingleUpdateTask(UPDATE_COMPONENT_PART2, componentInstanceIndex, UPDATE_STEP_WAIT, updateHandling_Part2_performUpdateTask_WAIT))
    {
        return false;
    }
    if(!updateHandling_enqueueSingleUpdateTask(UPDATE_COMPONENT_PART2, componentInstanceIndex, UPDATE_STEP_FW, updateHandling_Part2_performUpdateTask_FW))
    {
        return false;
    }
    return true;
}

/**********************************************************************/

size_t updateHandling_Part2_getInstanceCount()
{
#warning At the moment there are two instances of Part2. Make this variable.
    return 2;
}

/**********************************************************************/

char* updateHandling_Part2_queryVersion(int componentInstanceIndex = -1)
{
    switch (componentInstanceIndex)
    {
        case 0: return "?";
        case 1: return "?";
        default: return "?";
    }
    return "?"; // Default case, should not be reached
}