#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include "updateHandling_Part2.h"
#include "timeHandling.h"
#include "wifiHandling.h"
#include "part2ActionHub.h"
#include "certs.h"

/**********************************************************************/

update_info_t* currentUpdateInfoPart2;

AsyncWebServerRequest *pendingOtaRequest = nullptr;
bool otaProxyStartRequested = false;

// OTA proxy context
typedef struct ota_proxy_context
{
    std::unique_ptr<WiFiClientSecure> client;
    HTTPClient http;

    WiFiClient *stream = nullptr;

    bool finished = false;

    uint32_t lastDebugPrintTime = 0;
} ota_proxy_context_t;

/**********************************************************************/

// Cleanup helper
static void otaProxy_cleanup(AsyncWebServerRequest *request)
{
    if (!request || !request->_tempObject)
    {
        return;
    }

    ota_proxy_context_t *ctx = reinterpret_cast<ota_proxy_context_t *>(request->_tempObject);

    #ifdef DEBUG_OUTPUT
        Serial.println("Cleaning up OTA proxy context");
    #endif

    ctx->http.end();
    delete ctx;
    request->_tempObject = nullptr;
    yield();
}

/**********************************************************************/

// Route handler
void otaProxy_handlePart2(AsyncWebServerRequest *request)
{
    if(!part2ActionHub_isAPOpen)
    {
        request->send(500, "text/plain", "Access Point not open");
        #ifdef DEBUG_OUTPUT
            Serial.println("Access Point not open");
        #endif
        return;
    }

    if(currentUpdateInfoPart2 == nullptr || !currentUpdateInfoPart2->valid)
    {
        request->send(500, "text/plain", "No valid update info available");
        #ifdef DEBUG_OUTPUT
            Serial.println("No valid update info available");
        #endif
        return;
    }

    #ifdef DEBUG_OUTPUT
        Serial.println();
        Serial.println("=================================");
        Serial.println("OTA proxy request");
        Serial.println("=================================");
    #endif

    // Allocate persistent request context
    ota_proxy_context_t *ctx = new ota_proxy_context_t();

    if (!ctx)
    {
        request->send(500, "text/plain", "Failed to allocate context");
        return;
    }
    request->_tempObject = ctx;

    // Create HTTPS client
    ctx->client.reset(new WiFiClientSecure);
    if (!ctx->client)
    {
        otaProxy_cleanup(request);
        request->send(500, "text/plain", "Failed to allocate client");
        return;
    }

    ctx->client->setTrustAnchors(&certList);

    // Optional: reduce TLS memory usage
    //ctx->client->setBufferSizes(1024, 1024);

    // Configure HTTP client
    ctx->http.setTimeout(15000);
    ctx->http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    #ifdef DEBUG_OUTPUT
        Serial.println("Connecting to GitHub...");
        Serial.printf("Free heap before begin: %u\n", ESP.getFreeHeap());
    #endif

    String url = currentUpdateInfoPart2->url_fw;
    #ifdef DEBUG_OUTPUT
        Serial.printf("URL: %s\n", url.c_str());
    #endif
    if (!ctx->http.begin(*(ctx->client), url))
    {
        otaProxy_cleanup(request);
        request->send(500, "text/plain", "http.begin failed");
        return;
    }

    // Start HTTPS request
    int httpCode = ctx->http.GET();
    #ifdef DEBUG_OUTPUT
        Serial.printf("GitHub HTTP code: %d\n", httpCode);
    #endif
    if (httpCode != HTTP_CODE_OK)
    {
        otaProxy_cleanup(request);
        request->send(500, "text/plain", "GitHub download failed");
        return;
    }

    // Get stream
    ctx->stream = ctx->http.getStreamPtr();
    if (!ctx->stream)
    {
        otaProxy_cleanup(request);
        request->send(500, "text/plain", "Failed to get stream");
        return;
    }

    // Get content length
    int contentLength = ctx->http.getSize();
    #ifdef DEBUG_OUTPUT
        Serial.printf("Content-Length: %d\n", contentLength);
    #endif

    // Create chunked streaming response
    AsyncWebServerResponse *response = request->beginChunkedResponse("application/octet-stream", [request](uint8_t *buffer, size_t maxLen, size_t index) -> size_t
    {
        ota_proxy_context_t *ctx = reinterpret_cast<ota_proxy_context_t *>(request->_tempObject);
        if (!ctx)
        {
            return 0;
        }

        // Stream finished
        if (!ctx->stream->connected() && !ctx->stream->available())
        {
            #ifdef DEBUG_OUTPUT
                Serial.println("OTA proxy stream finished");
            #endif
            ctx->finished = true;
            otaProxy_cleanup(request);
            return 0;
        }

        // Wait for incoming data
        size_t available = ctx->stream->available();
        if (available == 0)
        {
            yield();
            delay(1);
            return 0;
        }

        // Read chunk
        size_t toRead = min(maxLen, available);
        size_t len = ctx->stream->readBytes(buffer, toRead);

        #ifdef DEBUG_OUTPUT
            // Debug output
            if (millis() - ctx->lastDebugPrintTime > 1000)
            {
                ctx->lastDebugPrintTime = millis();
                Serial.printf("Proxy streamed: %u bytes\n", index + len);
                Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
            }
        #endif

        yield();
        return len;
    });

    request->onDisconnect([request]()
    {
        #ifdef DEBUG_OUTPUT
            Serial.println("Client disconnected");
        #endif
        otaProxy_cleanup(request);
    });

    // Optional headers
    if (contentLength > 0)
    {
        response->addHeader("Content-Length", String(contentLength));
    }
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Connection", "close");

    // Send response
    request->send(response);

    #ifdef DEBUG_OUTPUT
        Serial.println("OTA proxy streaming started");
    #endif
}

/**********************************************************************/

void updateHandling_initWebserverEndpoints_Part2()
{
    //server.on("/fw/part2_fw.bin", HTTP_GET, otaProxy_handlePart2);

    server.on("/fw/part2_fw.bin", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if (otaProxyStartRequested || pendingOtaRequest)
        {
            request->send(503, "text/plain", "Busy");
            return;
        }
        pendingOtaRequest = request;
        otaProxyStartRequested = true;
    });

    #ifdef DEBUG_OUTPUT
        Serial.println("OTA proxy endpoints initialized");
    #endif
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

    if(!part2ActionHub_startAP()) { return false; }

#warning Just keep the AP open for 120 seconds for testing
    long startTime = millis();
    while(millis() - startTime < 120000)
    {
        if (otaProxyStartRequested)
        {
            otaProxyStartRequested = false;
            otaProxy_handlePart2(pendingOtaRequest);
        }

        yield();
    }

    part2ActionHub_stopAP();

#warning Update logic for part 2 is not implemented yet
    return false;
}