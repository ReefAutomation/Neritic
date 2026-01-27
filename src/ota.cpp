#include <Arduino.h>
#ifdef ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ArduinoOTA.h>
#include "esp_task_wdt.h"
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
            return false;
        }
        updateStarted = true;
    }
    size_t written = Update.write(buff, buffsize);
    if (written == buffsize) {
        totalBytesWritten += written;
        static uint8_t dotCount = 0;
        if (++dotCount >= 8) {
            debugPrint(".");
            dotCount = 0;
        }
        #ifdef ESP32
        esp_task_wdt_reset();
        #endif
        yield();
        return true;
    } else {
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
        http.end();
        latestVersion = "";
        return "";
    }
    String payload = http.getString();
    http.end();
    DynamicJsonDocument doc(2048); // Manifest is small
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
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
                return "";
            }
            return firmwareUrl;
        }
    }
    latestVersion = "";
    return "";
}

// Perform OTA update from the latest GitHub release for this environment
bool performGzOtaUpdate(String& errorOut) {
    otaInProgress = true;
    totalBytesWritten = 0;
    updateStarted = false;

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
    if (httpCode != HTTP_CODE_OK) {
        errorOut = String("HTTP error code: ") + httpCode;
        http.end();
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }

    int contentLength = http.getSize();
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
        static uint8_t dotCount = 0;
        if (++dotCount >= 8) {
            debugPrint(".");
            dotCount = 0;
        }
        #ifdef ESP32
        esp_task_wdt_reset();
        #endif
        yield();
    });

    // Use direct gzStreamExpander call, let callbacks handle watchdog/yield
    bool success = GZUnpacker->gzStreamExpander(stream, contentLength);
    delete GZUnpacker;
    http.end();


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
        otaInProgress = false;
        if (webServerPtr) webServerPtr->broadcastOtaStatus("error", errorOut);
        return false;
    }
}

// OTA direct POST handler (moved from webserver.cpp)
void handleOTAUpdate(AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total) {
    static unsigned int lastDot = 0;
    static File gzFile;
    static bool isGz = false;
    static size_t uploaded = 0;
    if (index == 0) {
        Serial.println("[HTTP] POST /ota");
        LittleFS.end();
        // Clean up any previous upload file to free space
        if (!LittleFS.begin()) {
            auto resp = request->beginResponse(500, "application/json", "{\"error\":\"LittleFS mount failed\"}");
            request->send(resp);
            return;
        }
        LittleFS.remove("/ota_upload.bin.gz");
        // Check gzip magic number
        isGz = (len >= 2 && data[0] == 0x1F && data[1] == 0x8B);
        uploaded = 0;
        if (isGz) {
            if (!LittleFS.begin()) {
                auto resp = request->beginResponse(500, "application/json", "{\"error\":\"LittleFS mount failed\"}");
                request->send(resp);
                return;
            }
            gzFile = LittleFS.open("/ota_upload.bin.gz", "w+");
            if (!gzFile) {
                auto resp = request->beginResponse(500, "application/json", "{\"error\":\"Failed to open file for writing\"}");
                request->send(resp);
                return;
            }
        } else {
#if defined(ESP32)
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
#elif defined(ESP8266)
            if (!Update.begin(total)) {
#endif
                auto resp = request->beginResponse(500, "application/json", "{\"error\":\"Update.begin failed\"}");
                request->send(resp);
                return;
            }
            lastDot = 0;
        }
    }
    if (isGz) {
        if (!gzFile || gzFile.write(data, len) != len) {
            auto resp = request->beginResponse(500, "application/json", "{\"error\":\"File write error\"}");
            request->send(resp);
            return;
        }
        uploaded += len;
        // Show a dot for every 64KB uploaded
        if (uploaded % 65536 < len) {
            debugPrint(".");
        }
    } else {
        if (Update.write(data, len) != len) {
            auto resp = request->beginResponse(500, "application/json", "{\"error\":\"Update write error\"}");
            request->send(resp);
            return;
        }
        // Print progress dots every 1%
        if (total > 0) {
            unsigned int dot = ((index + len) * 100) / total;
            if (dot != lastDot) {
                debugPrint(".");
                lastDot = dot;
            }
        }
    }
    if (index + len == total) {
        if (lastDot != 0) Serial.println(""); // Ensure LF after last dot
        lastDot = 0; // reset for next OTA
        AsyncWebServerResponse *resp = nullptr;
        bool ok = false; // Declare ok only once
        String errorMsg;
        if (isGz && gzFile) {
            gzFile.close();
            // Decompress and flash
            File inFile = LittleFS.open("/ota_upload.bin.gz", "r");
            if (!inFile) {
                resp = request->beginResponse(500, "application/json", "{\"error\":\"Failed to open uploaded gz file\"}");
                request->send(resp);
                return;
            }
            GzUnpacker *GZUnpacker = new GzUnpacker();
            totalBytesWritten = 0;
            updateStarted = false;
            GZUnpacker->setStreamWriter(gzWriteCallback);
            GZUnpacker->setGzProgressCallback([](uint8_t progress) {
                if (webServerPtr) webServerPtr->broadcastOtaStatus("progress", "Decompressing", progress);
            });
            ok = GZUnpacker->gzStreamExpander(&inFile, inFile.size());
            delete GZUnpacker;
            inFile.close();
            LittleFS.remove("/ota_upload.bin.gz");
            if (!ok) {
                errorMsg = "Decompression or flash failed";
            } else if (!updateStarted) {
                errorMsg = "Update never started - no data written";
                ok = false;
            } else if (!Update.end(true)) {
                errorMsg = String("Update error: ") + Update.getError();
                ok = false;
            } else if (!Update.isFinished()) {
                errorMsg = "Update not finished properly";
                ok = false;
            }
        } else {
            ok = Update.end(true);
            if (!ok) {
                errorMsg = String("Update error: ") + Update.getError();
            } else if (!Update.isFinished()) {
                errorMsg = "Update not finished properly";
                ok = false;
            }
        }
        if (ok) {
            resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
            Serial.println("OTA update complete, rebooting");
        } else {
            String errJson = String("{\"error\":\"") + errorMsg + "\"}";
            resp = request->beginResponse(500, "application/json", errJson);
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
    String error;
    bool ok = performGzOtaUpdate(error);
    if (ok) {
        debugPrintln("");
        debugPrintln("[OTA Task] OTA update successful, attempting restart...");
        delay(1000);
        ESP.restart();
        delay(5000);
        *((volatile int*)0) = 0; // Force a crash/reboot
    } else {
        debugPrint("[OTA Task] OTA update failed: ");
        debugPrintln(error);
    }
    vTaskDelete(NULL);
}
#endif
