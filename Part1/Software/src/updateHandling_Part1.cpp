#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include "updateHandling_Part1.h"
#include "timeHandling.h"
#include "wifiHandling.h"

/**********************************************************************/

bool updateHandling_performUpdatePart1(update_info_t& updateInfo, String component = "", int componentInstanceIndex = -1)
{
#warning componentInstanceIndex is currently not really used...

    if (!updateInfo.valid)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] No valid update info available"));
        #endif
        return false;
    }

    if(isTimeValid == false)
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] Time is not valid yet, cannot check for updates because SSL certificate validation will fail. Try again later..."));
        #endif
        return false;
    }

    #ifdef DEBUG_OUTPUT
        Serial.printf_P(PSTR("[Update Handling Part1] Performing update for component %s, index %d to version %s\n"), component.c_str(), componentInstanceIndex, updateInfo.version.c_str());
        Serial.printf_P(PSTR("[Update Handling Part1] Free heap before SSL: %u bytes\n"), ESP.getFreeHeap());
    #endif

    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.setClientTimeout(10000);

    updateStatus.updateStep = UPDATE_STEP_FW;

    static int lastLoggedPercent = -1;

    ESPhttpUpdate.onStart([&updateInfo]()
    {
        if (!updateInfo.has_fs_update || (updateInfo.has_fs_update && updateStatus.updateStep == UPDATE_STEP_FS))
        {
            lastLoggedPercent = -1;
            updateStatus.updateProgress = 0.0f;
        }
    });
    ESPhttpUpdate.onEnd([&updateInfo]()
    {
        if (!updateInfo.has_fs_update || (updateInfo.has_fs_update && updateStatus.updateStep == UPDATE_STEP_FW))
        {
            updateStatus.updateProgress = 100.0f;
        }
    });
    ESPhttpUpdate.onProgress([&updateInfo](int cur, int total)
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
        if(!updateInfo.has_fs_update)
        {
            updateStatus.updateProgress = percent;
        }
        else
        {
            if(updateStatus.updateStep == UPDATE_STEP_FS)
            {
                // First half of the update progress is for the filesystem update, which is performed before the firmware update.
                updateStatus.updateProgress = percent * 0.5f;
            }
            else
            {
                // Second half of the update progress is for the firmware update, which is performed after the filesystem update.
                updateStatus.updateProgress = 50.0f + (percent * 0.5f);
            }
        }
                
        yield(); // Yield to allow other tasks to run (e.g. webserver)
    });

    WiFiClientSecure clientSecure;
    clientSecure.setSession(&session);
    clientSecure.setTrustAnchors(&certList);
    clientSecure.setBufferSizes(16384, 512);

    bool fsUpdateResult = true;
    if(updateInfo.has_fs_update)
    {
        updateStatus.updateStep = UPDATE_STEP_FS;
        #ifdef DEBUG_OUTPUT
            Serial.println(F("[Update Handling Part1] Update file system..."));
        #endif
        ESPhttpUpdate.setMD5sum(updateInfo.fs_md5);
        t_httpUpdate_return returnFsUpdate = ESPhttpUpdate.updateFS(clientSecure, updateInfo.url_fs);
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
        updateStatus.updateStep = UPDATE_STEP_FW;
    }

    bool fwUpdateResult = true;
    #ifdef DEBUG_OUTPUT
        Serial.println(F("[Update Handling Part1] Update firmware..."));
    #endif
    ESPhttpUpdate.setMD5sum(updateInfo.fw_md5);
    ESPhttpUpdate.rebootOnUpdate(false);    // Don't reboot automatically after the firmware update.
    t_httpUpdate_return returnFwUpdate = ESPhttpUpdate.update(clientSecure, updateInfo.url_fw);
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

    if(fwUpdateResult)
    {
        updateStatus.state = UPDATE_STATE_RESTARTING;
    
        // Wait for some seconds to ensure that the HTTP response is sent completely before restarting.
        // This is especially important if the update was triggered via the web interface, because otherwise the web interface might not receive the response and thus not know that the update was successful.
        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            yield();
        }
        ESP.restart();
    }
    else
    {
        updateStatus.state = UPDATE_STATE_ERROR;
    }

    return fsUpdateResult && fwUpdateResult;
}