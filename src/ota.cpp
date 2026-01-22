#include <Arduino.h>
#ifdef ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ArduinoOTA.h>
#endif
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClientSecure.h>
#ifdef ESP32
#include <HTTPClient.h>
#else
#include <ESP8266HTTPClient.h>
#endif
#ifdef ESP32
#include <Update.h>
#else
#include <Updater.h>
#endif
#include <ESP32-targz.h>
#include "config.h"
#include "scheduler.h"
#include "transition.h"
#include "webserver.h"
#include "ota.h"

extern WebServerManager* webServerPtr; // Must be set to the global instance

volatile bool otaInProgress = false;
volatile bool otaRequested = false;

void setupArduinoOTA(const char* hostname) {
#ifdef ESP32
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.onStart([]() {
        otaInProgress = true;
    });
    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
    });
    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    });
    ArduinoOTA.begin();
#endif
}

void handleArduinoOTA() {
#ifdef ESP32
    ArduinoOTA.handle();
#endif
}

// Write callback for decompressed data (main-loop safe)
static size_t totalBytesWritten = 0;
static bool updateStarted = false;
static bool gzWriteCallback(unsigned char* buff, size_t buffsize) {
    if (!updateStarted) {
        if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
            Serial.println("Failed to begin update");
            return false;
        }
        updateStarted = true;
        Serial.println("Update started, writing decompressed data...");
    }
    size_t written = Update.write(buff, buffsize);
    if (written == buffsize) {
        totalBytesWritten += written;
        if (totalBytesWritten % 102400 < 4096) {
                Serial.printf("Written: %d KB\n", totalBytesWritten / 1024);
        }
        yield(); // allow background tasks to run
        return true;
    } else {
        Serial.printf("Write error: only wrote %d of %d bytes\n", written, buffsize);
        return false;
    }
}

// Fetch the latest firmware URL for this environment from GitHub
String getLatestFirmwareUrl(String& latestVersion) {
    // Download manifest.json from the latest release
    const char* manifestUrl = "https://github.com/kabroxiko/DeepGlow/releases/latest/download/manifest.json";
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, manifestUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] Manifest download error: %d\n", httpCode);
        http.end();
        latestVersion = "";
        return "";
    }
    String payload = http.getString();
    http.end();
    DynamicJsonDocument doc(2048); // Manifest is small
    Serial.println("[OTA] Raw manifest.json:");
    Serial.println(payload);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[OTA] Failed to parse manifest.json: ");
        Serial.println(err.c_str());
        latestVersion = "";
        return "";
    }
    // Manifest is an array of objects: [{ type, env, version, url }]
    const char* targetEnv = OTA_ENV;
    for (JsonVariant entry : doc.as<JsonArray>()) {
        String env = entry["env"].as<String>();
        if (env == targetEnv) {
            latestVersion = entry["version"].as<String>();
            String firmwareUrl = entry["url"].as<String>();
            if (firmwareUrl.length() == 0) {
                Serial.println("[OTA] No url in manifest entry");
                return "";
            }
            Serial.printf("[OTA] Manifest firmware_url: %s\n", firmwareUrl.c_str());
            return firmwareUrl;
        }
    }
    Serial.println("[OTA] No matching env entry in manifest");
    latestVersion = "";
    return "";
}

// Perform OTA update from the latest GitHub release for this environment
bool performGzOtaUpdate(String& errorOut) {
    otaInProgress = true;
    totalBytesWritten = 0;
    updateStarted = false;

    Serial.printf("[OTA] Free heap at start: %u\n", ESP.getFreeHeap());
    if (webServerPtr) webServerPtr->broadcastOtaStatus("start", "OTA update started");

    String latestVersion;
    String firmwareUrl = getLatestFirmwareUrl(latestVersion);
    if (firmwareUrl.isEmpty()) {
        errorOut = "Could not determine latest firmware URL.";
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }

    // TODO: Compare latestVersion to current version, skip if not newer

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    HTTPClient http;
    http.begin(client, firmwareUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("ESP32-OTA-Updater");

    int httpCode = http.GET();
    Serial.printf("[OTA] HTTP GET %s -> code %d\n", firmwareUrl.c_str(), httpCode);
    if (httpCode != HTTP_CODE_OK) {
        errorOut = String("HTTP error code: ") + httpCode;
        http.end();
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }

    int contentLength = http.getSize();
    Serial.printf("[OTA] Content length: %d\n", contentLength);
    if (contentLength <= 0) {
        errorOut = "Invalid content length";
        http.end();
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();

    GzUnpacker *GZUnpacker = new GzUnpacker();
    GZUnpacker->setStreamWriter(gzWriteCallback);
    GZUnpacker->setGzProgressCallback([](uint8_t progress) {
        if (webServerPtr) webServerPtr->broadcastOtaStatus("progress", "Decompressing", progress);
        static uint8_t lastProgress = 0;
        if (progress != lastProgress && progress % 10 == 0) {
            Serial.printf("Decompression progress: %d%%\n", progress);
            lastProgress = progress;
        }
    });

    bool success = GZUnpacker->gzStreamExpander(stream, contentLength);
    delete GZUnpacker;
    http.end();

    Serial.printf("[OTA] Free heap after decompress: %u\n", ESP.getFreeHeap());

    if (!success) {
        errorOut = "Decompression failed!";
        if (updateStarted) {
    #ifdef ESP32
            Update.abort();
    #else
            Update.end(false);
    #endif
        }
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }

    if (!updateStarted) {
        errorOut = "Update never started - no data written";
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }

    if (Update.end(true)) {
        if (Update.isFinished()) {
            otaInProgress = false;
            Serial.printf("[OTA] Free heap after update: %u\n", ESP.getFreeHeap());
            if (webServerPtr) webServerPtr->broadcastOtaStatus("success", "OTA update successful");
            return true;
        } else {
            errorOut = "Update not finished properly";
            otaInProgress = false;
            if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
            return false;
        }
    } else {
        errorOut = String("Update error: ") + Update.getError();
        Update.printError(Serial);
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }
}

// OTA direct POST handler (moved from webserver.cpp)
void handleOTAUpdate(AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total) {
    static unsigned int lastDot = 0;
    if (index == 0) {
        LittleFS.end(); // Free filesystem before OTA
    #if defined(ESP32)
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    #elif defined(ESP8266)
        if (!Update.begin(total)) {
    #endif
            Update.printError(Serial);
        }
        lastDot = 0;
        Serial.println("OTA update started");
    }
    if (Update.write(data, len) != len) {
            Update.printError(Serial);
    }
    // Print progress dots every 1%
    if (total > 0) {
        unsigned int dot = ((index + len) * 100) / total;
        while (lastDot < dot) {
            lastDot++;
            Serial.print(".");
        }
    }
    if (index + len == total) {
        Serial.println("");
        lastDot = 0; // reset for next OTA
        bool ok = Update.end(true);
        AsyncWebServerResponse *resp = nullptr;
        if (ok) {
            resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
            Serial.println("OTA update complete, rebooting");
        } else {
            Update.printError(Serial);
            resp = request->beginResponse(500, "application/json", "{\"error\":\"OTA Update Failed\"}");
            Serial.println("OTA update failed");
        }
        for (size_t i = 0; i < 3; ++i) resp->addHeader("Access-Control-Allow-Origin", "*");
        request->send(resp);
        if (ok) {
            request->onDisconnect([]() {
                delay(100);
                ESP.restart();
            });
        }   
    }
}

#ifdef ESP32
extern "C" void otaTask(void* parameter) {
    Serial.println("[OTA Task] Started. Checking for update...");
    String error;
    bool ok = performGzOtaUpdate(error);
    if (ok) {
        Serial.println("[OTA Task] OTA update successful, attempting restart...");
        delay(1000);
        Serial.println("[OTA Task] Calling ESP.restart()...");
        ESP.restart();
        delay(5000);
        Serial.println("[OTA Task] ESP.restart() did not work, forcing crash.");
        *((volatile int*)0) = 0; // Force a crash/reboot
    } else {
        Serial.print("[OTA Task] OTA update failed: ");
        Serial.println(error);
    }
    Serial.println("[OTA Task] Exiting task.");
    vTaskDelete(NULL);
}
#endif
