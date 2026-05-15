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
#include "certs.h"

/**********************************************************************/

#define LITTLEFS_PART2_FW_PATH "/fw/part2_fw.bin"

update_info_t* currentUpdateInfoPart2;

/**********************************************************************/

// Hilfsfunktion zur Berechnung des MD5-Hashes einer Datei im LittleFS
String calculateFileMD5(const String &filePath)
{
    File file = LittleFS.open(filePath, "r");
    if (!file)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("Failed to open file %s for MD5 calculation\n", filePath.c_str());
        #endif
        return "";
    }

    MD5Builder md5;
    md5.begin();
    uint8_t buffer[512];
    while (file.available())
    {
        size_t bytesRead = file.read(buffer, sizeof(buffer));
        md5.add(buffer, bytesRead);
    }
    file.close();
    md5.calculate();
    return md5.toString();
}

void updateHandling_part2ReportProgress(float stepProgress)
{
    updateStatus.updateProgress = stepProgress;
    updateHandling_sendProgressEvent(updateStatus.updateProgress);
}

bool updateHandling_downloadFileToLittleFS(const String &url, const String &filePath, const String &expectedMd5)
{
    #ifdef DEBUG_OUTPUT
        Serial.println("Downloading file...");
    #endif
    WiFiClientSecure client;
    client.setTrustAnchors(&certList);

    // Check, if the file already exists and the MD5 hash matches
    if (LittleFS.exists(filePath))
    {
        String currentMd5 = calculateFileMD5(filePath);
        if (currentMd5 == expectedMd5)
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf("File %s already exists and MD5 matches. Skipping download.\n", filePath.c_str());
            #endif
            return true;
        }
        else
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf("File %s exists but MD5 mismatch (expected: %s, actual: %s). Deleting and re-downloading.\n", filePath.c_str(), expectedMd5.c_str(), currentMd5.c_str());
            #endif
            LittleFS.remove(filePath); // Remove old file
        }
    }

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, url))
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("http.begin failed");
        #endif
        return false;
    }

    int httpCode = http.GET();
    #ifdef DEBUG_OUTPUT
        Serial.printf("HTTP code: %d\n", httpCode);
    #endif
    if (httpCode != HTTP_CODE_OK)
    {
        http.end();
        #ifdef DEBUG_OUTPUT
            Serial.println("HTTP error");
        #endif
        return false;
    }

    File file = LittleFS.open(filePath, "w");
    if (!file)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("Failed to open file");
        #endif
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[512];
    uint32_t totalWritten = 0;

    while (http.connected() || stream->available())
    {
        if (contentLength > 0 && totalWritten >= (uint32_t)contentLength)
        {
            break;
        }

        size_t available = stream->available();
        if (available)
        {
            size_t len = stream->readBytes(buffer, min(available, (size_t)sizeof(buffer)));
            file.write(buffer, len);
            totalWritten += len;

            float percent = (contentLength > 0) ? (100.0f * totalWritten / contentLength) : 0.0f;
            updateHandling_part2ReportProgress(percent);
            #ifdef DEBUG_OUTPUT
                Serial.printf("Downloaded: %u bytes  -> %.2f%%\n", totalWritten, percent);
            #endif
        }
        yield();
    }

    file.close();
    http.end();

    // Check MD5 hash after download
    String downloadedMd5 = calculateFileMD5(filePath);
    if (downloadedMd5 != expectedMd5)
    {
        #ifdef DEBUG_OUTPUT
            Serial.printf("Downloaded file MD5 mismatch! Expected: %s, Actual: %s. Deleting file.\n", expectedMd5.c_str(), downloadedMd5.c_str());
        #endif
        LittleFS.remove(filePath); // Defect file -> delete it
        return false;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println("Download finished and MD5 verified.");
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
        updateHandling_setUpdateStep(UPDATE_STEP_FW);
        request->send(LittleFS, LITTLEFS_PART2_FW_PATH, "application/octet-stream");
    });
    server.on("/update/part2_fw_md5", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if(currentUpdateInfoPart2 == nullptr || !currentUpdateInfoPart2->valid)
        {
            request->send(404, "text/plain", "No valid update info available");
            return;
        }
        request->send(200, "text/plain", currentUpdateInfoPart2->fw_md5);
    });
    server.on("/update/part2_report", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        if (request->hasParam("progress", true))
        {
            updateStatus.updateProgress = request->getParam("progress", true)->value().toFloat();
            updateHandling_sendProgressEvent(updateStatus.updateProgress);
        }
        if (request->hasParam("finished", true) && request->getParam("finished", true)->value() == "true")
        {
            updateHandling_setUpdateStep(UPDATE_STEP_FINISHED);
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
            Serial.println("No valid update info available");
        #endif
        return false;
    }

    if(isTimeValid == false)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("Time is not valid yet, cannot check for updates because SSL certificate validation will fail. Try again later...");
        #endif
        return false;
    }

    currentUpdateInfoPart2 = &updateInfo;

    updateHandling_setUpdateStep(UPDATE_STEP_PREPARE);
    if(!updateHandling_downloadFileToLittleFS(updateInfo.url_fw, LITTLEFS_PART2_FW_PATH, updateInfo.fw_md5)) { return false; }

    #ifdef DEBUG_OUTPUT
        if(updateInfo.has_fs_update)
        {
            Serial.println("Filesystem update not supported yet for part 2");
        }
    #endif

    if(!part2ActionHub_startAP()) { return false; }

    updateHandling_setUpdateStep(UPDATE_STEP_WAIT);
    UpdateStep lastStep = updateStatus.updateStep;
    while(updateStatus.updateStep != UPDATE_STEP_FINISHED)
    {
        if(updateStatus.updateStep != lastStep)
        {
            #ifdef DEBUG_OUTPUT
                Serial.printf("Update step changed to %d\n", updateStatus.updateStep);
            #endif
            updateHandling_sendUpdateStatusEvent();
            lastStep = updateStatus.updateStep;
        }

        if(part2ActionHub_handleAPTimeout())
        {
            // The action hub was closed by timeout.
            updateHandling_setUpdateStep(UPDATE_STEP_NONE);
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