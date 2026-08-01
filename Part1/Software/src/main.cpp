#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "main.h"
#include "config.h"
#include "wifiHandling.h"
#include "timeHandling.h"
#include "version.h"
#include "updateHandling.h"
#include "part2ActionHub.h"

void main_initWebserverEndpoints()
{
    server.on("/eraseCredentials", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // Send back the response before erasing the credentials, otherwise the client would not receive the response because the device restarts immediately after erasing the credentials
        request->send(200, "text/plain", "Credentials erased"); 
        wifiHandling_eraseCredentials();
    });

    server.on("/createTestFile", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // Determine which file to create based on optional 'type' parameter
        // type=0: testfile.txt (default)
        // type=1: data0.bin
        // type=2: data1.bin
        int fileType = 0;
        if (request->hasParam("type"))
        {
            fileType = request->getParam("type")->value().toInt();
        }
        
        String filename;
        String fileDesc;
        
        switch (fileType)
        {
            case 1:
                filename = "/data0.bin";
                fileDesc = "Data File 0";
                break;
            case 2:
                filename = "/data1.bin";
                fileDesc = "Data File 1";
                break;
            default:
                filename = "/testfile.txt";
                fileDesc = "Test File";
                break;
        }
        
        File testFile = LittleFS.open(filename.c_str(), "w");    // open file and overwrite if it already exists
        if (!testFile)
        {
            request->send(500, "text/plain", "Failed to create test file");
            return;
        }
        
        // Write test content with timestamp and version info
        testFile.print(fileDesc);
        testFile.println(" Created");
        testFile.print("FW Version: ");
        testFile.println(FW_VERSION);
        testFile.print("Filename: ");
        testFile.println(filename);
        testFile.println("Created for backup/restore testing");
        testFile.close();
        
        request->send(200, "text/plain", "Test file created successfully");
    });

    server.on("/getTestFile", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // Determine which file to read based on optional 'type' parameter
        // type=0: testfile.txt (default)
        // type=1: data0.bin
        // type=2: data1.bin
        int fileType = 0;
        if (request->hasParam("type"))
        {
            fileType = request->getParam("type")->value().toInt();
        }
        
        String filename;
        
        switch (fileType)
        {
            case 1:
                filename = "/data0.bin";
                break;
            case 2:
                filename = "/data1.bin";
                break;
            default:
                filename = "/testfile.txt";
                break;
        }
        
        if (!LittleFS.exists(filename.c_str()))
        {
            request->send(404, "text/plain", "Test file does not exist");
            return;
        }
        
        File testFile = LittleFS.open(filename.c_str(), "r");
        if (!testFile)
        {
            request->send(500, "text/plain", "Failed to read test file");
            return;
        }
        
        String content = testFile.readString();
        testFile.close();
        
        request->send(200, "text/plain", content);
    }); 
}

/**********************************************************************/

void setup()
{
    #ifdef DEBUG_OUTPUT
        Serial.begin(115200);
    #endif
    pinMode(LED_BUILTIN, OUTPUT);

    #ifdef DEBUG_OUTPUT
        Serial.print(F("FW Version: "));
        Serial.println(F(FW_VERSION));
    #endif

    // Begin LittleFS
    if (!LittleFS.begin())
    {
        #ifdef DEBUG_OUTPUT
            Serial.println(F("An Error has occurred while mounting LittleFS"));
        #endif
        return;
    }

    timeHandling_init();
    wifiHandling_init();
    delay(1000);
    timeHandling_printNowSerial();

    digitalWrite(LED_BUILTIN, LOW);            // Turn on LED
}

/**********************************************************************/

void loop()
{
    wifiHandling_loop();
    if(isTimeValid)
    {
        updateHandling_loop();
    }
    part2ActionHub_handleAPTimeout();
}
