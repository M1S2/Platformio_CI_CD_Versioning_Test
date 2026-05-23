#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MD5Builder.h>
#include "updateHandling_Part2.h"
#include "timeHandling.h"
#include "wifiHandling.h"
#include "part2ActionHub.h"

/**********************************************************************/

#define LITTLEFS_PART2_FW_PATH "/fw/part2_fw.bin"

update_info_t* currentUpdateInfoPart2;

/**********************************************************************/

// Calculate the MD5 hash of a file in the LittleFS
String calculateFileMD5(const String &filePath)
{
    File file = LittleFS.open(filePath, "r");
    if (!file)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling Part2] Failed to open file %s for MD5 calculation\n", filePath.c_str());
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

#if false
void updateHandling_part2ReportProgress(float stepProgress)
{
    updateStatus.updateProgress = stepProgress;
    updateHandling_sendProgressEvent(updateStatus.updateProgress);
}
#endif

bool updateHandling_downloadFileToLittleFS(const String &url, const String &filePath, const String &expectedMd5)
{
    #ifdef DEBUG_OUTPUT
        Serial.println("[Update Handling Part2] Downloading file...");
    #endif

    // Check, if the file already exists and the MD5 hash matches
    if (LittleFS.exists(filePath))
    {
        String currentMd5 = calculateFileMD5(filePath);
        if (currentMd5 == expectedMd5)
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf("[Update Handling Part2] File %s already exists and MD5 matches. Skipping download.\n", filePath.c_str());
            #endif
            return true;
        }
        else
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf("[Update Handling Part2] File %s exists but MD5 mismatch (expected: %s, actual: %s). Deleting and re-downloading.\n", filePath.c_str(), expectedMd5.c_str(), currentMd5.c_str());
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
            Serial.println("[Update Handling Part2] http.begin failed");
        #endif
        http.end();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling Part2] HTTP error: Code = %d, Message = %s\n", httpCode, http.errorToString(httpCode).c_str());
        #endif
        http.end();
        return false;
    }

    File file = LittleFS.open(filePath, "w");
    if (!file)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling Part2] Failed to open file");
        #endif
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling Part2] Failed to get stream pointer");
        #endif
        file.close();
        http.end();
        return false;
    }
    #ifdef DEBUG_OUTPUT
        Serial.printf("[Update Handling Part2] Stream successfully opened, %d bytes to download\n", contentLength);
    #endif

    // Use stack buffer to avoid heap fragmentation during SSL download
    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];

    uint32_t totalWritten = 0;

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
            updateStatus.updateProgress = percent;

            #ifdef DEBUG_OUTPUT
                Serial.printf("[Update Handling Part2] Downloaded: %u bytes  -> %.2f%%\n", totalWritten, percent);
            #endif
        }
        yield();
    }

    file.close();
    http.end();

    // Check if the download was actually complete
    if (contentLength > 0 && totalWritten < (uint32_t)contentLength)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling Part2] Download failed: Interrupted at %u of %d bytes\n", totalWritten, contentLength);
        #endif
        return false;
    }

    // Check MD5 hash after download
    String downloadedMd5 = calculateFileMD5(filePath);
    if (downloadedMd5 != expectedMd5)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("[Update Handling Part2] Downloaded file MD5 mismatch! Expected: %s, Actual: %s. Deleting file.\n", expectedMd5.c_str(), downloadedMd5.c_str());
        #endif
        LittleFS.remove(filePath); // Defect file -> delete it
        return false;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println("[Update Handling Part2] Download finished and MD5 verified.");
    #endif
    return true;
}

/**********************************************************************/

void updateHandling_initWebserverEndpoints_Part2()
{
    server.on("/update/part2_fw.bin", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if(!part2ActionHub_isAPOpen)
        {
            request->send(404, "text/plain", "Action hub not open");
            return;
        }
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
        if(currentUpdateInfoPart2 == nullptr || !currentUpdateInfoPart2->valid)
        {
            request->send(404, "text/plain", "No valid update info available");
            return;
        }
        request->send(200, "text/plain", currentUpdateInfoPart2->fw_md5);
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
            updateStatus.updateStep = UPDATE_STEP_FINISHED;
        }
        request->send(200, "text/plain", "OK");
    });
}

/**********************************************************************/

bool updateHandling_performUpdatePart2(update_info_t& updateInfo, String component = "", int componentInstanceIndex = -1)
{
    if (!updateInfo.valid)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling Part2] No valid update info available");
        #endif
        return false;
    }

    if(isTimeValid == false)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("[Update Handling Part2] Time is not valid yet, cannot check for updates because SSL certificate validation will fail. Try again later...");
        #endif
        return false;
    }

    currentUpdateInfoPart2 = &updateInfo;

    updateStatus.updateStep = UPDATE_STEP_PREPARE;
    if(!updateHandling_downloadFileToLittleFS(updateInfo.url_fw, LITTLEFS_PART2_FW_PATH, updateInfo.fw_md5)) { return false; }

    #ifdef DEBUG_OUTPUT
        if(updateInfo.has_fs_update)
        {
            Serial.println("[Update Handling Part2] Filesystem update not supported yet for part 2");
        }
    #endif

    if(!part2ActionHub_startAP(PART2ACTIONHUB_ACTION_UPDATE)) { return false; }

    updateStatus.updateStep = UPDATE_STEP_WAIT;
    UpdateStep lastStep = updateStatus.updateStep;
    while(updateStatus.updateStep != UPDATE_STEP_FINISHED)
    {
        if(updateStatus.updateStep != lastStep)
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf("[Update Handling Part2] Update step changed to %d\n", updateStatus.updateStep);
            #endif
            lastStep = updateStatus.updateStep;
        }

        if(part2ActionHub_handleAPTimeout())
        {
            // The action hub was closed by timeout.
            updateStatus.updateStep = UPDATE_STEP_NONE;
        }
        yield();
    }

    part2ActionHub_stopAP();
    // Cleanup downloaded file
    if(LittleFS.exists(LITTLEFS_PART2_FW_PATH))
    {
        LittleFS.remove(LITTLEFS_PART2_FW_PATH);
    }

    if(updateStatus.updateStep == UPDATE_STEP_FINISHED)
    {
        return true;
    }
    return false;
}