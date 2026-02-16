#define DEEPGLOW_REPO_URL "https://github.com/kabroxiko/DeepGlow"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#ifdef ESP32
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#else
#include <ESP8266HTTPClient.h>
#include <Updater.h>
#endif
#include "webserver.h"

#include "config.h"
#include "ota.h"
#include "scheduler.h"
#include "transition.h"
#include "webserver.h"

#include <ESP32-targz.h>

// Forward declaration for OTA status broadcast
static void broadcastOtaStatus(const String &status, const String &msg, int progress);


extern WebServerManager *webServerPtr; // Must be set to the global instance

volatile bool otaInProgress = false;
volatile bool otaRequested = false;
// Global flag for OTA ACK handshake
volatile bool otaAckReceived = false;

// Fetch the latest manifest JSON from the remote repository (returns empty string on failure)
String fetchRemoteManifestJson() {
  const char *manifestUrl = DEEPGLOW_REPO_URL "/releases/latest/download/manifest.json";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, manifestUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return "";
  }
  String payload = http.getString();
  http.end();
  return payload;
}

void setupArduinoOTA(const char *hostname) {
#ifdef ESP32
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() { otaInProgress = true; });
  ArduinoOTA.onEnd([]() { otaInProgress = false; });
  ArduinoOTA.onError([](ota_error_t error) { otaInProgress = false; });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.begin();
#endif
}

void handleArduinoOTA() {
#ifdef ESP32
  ArduinoOTA.handle();
#endif
}

// Write callback for decompressed data (main-loop safe)
// Track last progress globally for finalization
static size_t totalBytesWritten = 0;
static bool updateStarted = false;
static int otaContentLength = 0; // Add static variable for content length
static bool gzWriteCallback(unsigned char *buff, size_t buffsize) {
  if (!updateStarted) {
    if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
      return false;
    }
    updateStarted = true;
  }
  size_t written = Update.write(buff, buffsize);
  if (written == buffsize) {
    totalBytesWritten += written;
    // OTA progress: totalBytesWritten vs estimated decompressed size
    size_t estimatedTotal = otaContentLength * 1.5;
    int progress = 0;
    if (estimatedTotal > 0) {
      progress = (totalBytesWritten * 100) / estimatedTotal;
      if (progress > 100) progress = 100;
    }
    static int lastProgressPercent = -1;
    if (progress != lastProgressPercent) {
      broadcastOtaStatus("progress", String(totalBytesWritten), progress);
      lastProgressPercent = progress;
    }
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
String getLatestFirmwareUrl(String &latestVersion) {
  // Download manifest.json from the latest release
  const char *manifestUrl = DEEPGLOW_REPO_URL "/releases/latest/download/manifest.json";
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
  const char *targetEnv = OTA_ENV;
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

// Helper: Broadcast OTA status if webServerPtr is set
static void broadcastOtaStatus(const String &status, const String &msg, int progress) {
  // Debug: Print all OTA status broadcasts
  if (progress >= 0) {
    debugPrintln("[OTA][broadcast] status=%s, msg=%s, progress=%d", status.c_str(), msg.c_str(), progress);
  } else {
    debugPrintln("[OTA][broadcast] status=%s, msg=%s", status.c_str(), msg.c_str());
  }
  if (webServerPtr) {
    if (progress >= 0)
      webServerPtr->broadcastOtaStatus(status, msg, progress);
    else
      webServerPtr->broadcastOtaStatus(status, msg);
  }
}

// Helper: Download firmware and return HTTPClient and stream pointer
static bool downloadFirmware(const String &firmwareUrl, HTTPClient &http, WiFiClientSecure &client, int &contentLength, String &errorOut) {
  client.setInsecure();
  client.setTimeout(30);
  http.begin(client, firmwareUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("ESP32-OTA-Updater");
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    errorOut = String("HTTP error code: ") + httpCode;
    http.end();
    return false;
  }
  contentLength = http.getSize();
  if (contentLength <= 0) {
    errorOut = "Invalid content length";
    http.end();
    return false;
  }
  return true;
}

// Helper: Decompress and update firmware
static bool decompressAndUpdate(WiFiClient *stream, int contentLength, String &errorOut) {
  GzUnpacker *GZUnpacker = new GzUnpacker();
  totalBytesWritten = 0;
  updateStarted = false;
  otaContentLength = contentLength; // Set static variable for use in callback
  GZUnpacker->setStreamWriter(gzWriteCallback);
  bool success = GZUnpacker->gzStreamExpander(stream, contentLength);
  delete GZUnpacker;
  if (!success) {
    errorOut = "Decompression failed!";
    if (updateStarted) {
#ifdef ESP32
      Update.abort();
#else
      Update.end(false);
#endif
    }
    return false;
  }
  if (!updateStarted) {
    errorOut = "Update never started - no data written";
    return false;
  }
  return true;
}

// Helper: Finalize update and check result


static bool finalizeUpdate(String &errorOut) {
  bool endResult = Update.end(true);
  int errCode = Update.getError();
  String errMsg;
  #ifdef ESP32
    errMsg = Update.errorString();
  #else
    errMsg = Update.getErrorString();
  #endif
  if (endResult) {
    if (Update.isFinished()) {
      return true;
    } else {
      errorOut = "Update not finished properly";
      return false;
    }
  } else {
    // If error code is 0 (No Error), retry once
    if (errCode == 0) {
      endResult = Update.end(true);
      errCode = Update.getError();
      #ifdef ESP32
        errMsg = Update.errorString();
      #else
        errMsg = Update.getErrorString();
      #endif
      if (endResult && Update.isFinished()) {
        return true;
      }
    }
    errorOut = String("Update error: ") + errCode + " (" + errMsg + ")";
    return false;
  }
}

bool performGzOtaUpdate(String &errorOut) {
  otaInProgress = true;
  totalBytesWritten = 0;
  updateStarted = false;

  broadcastOtaStatus("start", "OTA update started", -1);

  String latestVersion;
  String firmwareUrl = getLatestFirmwareUrl(latestVersion);
  if (firmwareUrl.isEmpty()) {
    errorOut = "Could not determine latest firmware URL.";
    otaInProgress = false;
    broadcastOtaStatus("error", errorOut, -1);
    return false;
  }

  // TODO: Compare latestVersion to current version, skip if not newer

  WiFiClientSecure client;
  HTTPClient http;
  int contentLength = 0;
  if (!downloadFirmware(firmwareUrl, http, client, contentLength, errorOut)) {
    otaInProgress = false;
    broadcastOtaStatus("error", errorOut, -1);
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  bool ok = decompressAndUpdate(stream, contentLength, errorOut);
  http.end();
  ok = finalizeUpdate(errorOut);
  otaInProgress = false;
  if (ok) {
    // Always send a final 100% progress update before success
    broadcastOtaStatus("progress", String(totalBytesWritten), 100);
    broadcastOtaStatus("success", "OTA update successful", -1);
    return true;
  } else {
    // Always broadcast the real error message
    String errMsg = errorOut.length() > 0 ? errorOut : "OTA failed: unknown error";
    broadcastOtaStatus("error", errMsg, -1);
    return false;
  }
}

// OTA direct POST handler (moved from webserver.cpp)

// Helper: Respond with error and return
static void otaRespondError(AsyncWebServerRequest *request, const String &msg) {
  auto resp = request->beginResponse(500, "application/json", String("{\"error\":\"") + msg + "\"}");
  request->send(resp);
}

// Helper: Begin OTA upload (index==0)
static bool otaBeginUpload(AsyncWebServerRequest *request, unsigned char *data, unsigned int len, unsigned int total, File &gzFile, bool &isGz, size_t &uploaded, unsigned int &lastDot) {
  otaInProgress = true;
  debugPrintln("[OTA] Begin upload");
  LittleFS.end();
  if (!LittleFS.begin()) {
    debugPrintln("[OTA] LittleFS mount failed");
    otaRespondError(request, "LittleFS mount failed");
    return false;
  }
  LittleFS.remove("/ota_upload.bin.gz");
  isGz = (len >= 2 && data[0] == 0x1F && data[1] == 0x8B);
  uploaded = 0;
  if (isGz) {
    debugPrintln("[OTA] Detected gzipped upload");
    if (!LittleFS.begin()) {
      debugPrintln("[OTA] LittleFS mount failed (gz)");
      otaRespondError(request, "LittleFS mount failed");
      return false;
    }
    gzFile = LittleFS.open("/ota_upload.bin.gz", "w+");
    if (!gzFile) {
      debugPrintln("[OTA] Failed to open file for writing");
      otaRespondError(request, "Failed to open file for writing");
      return false;
    }
  } else {
#if defined(ESP32)
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
#elif defined(ESP8266)
    if (!Update.begin(total)) {
#endif
      debugPrintln("[OTA] Update.begin failed");
      otaRespondError(request, "Update.begin failed");
      return false;
    }
    debugPrintln("[OTA] Update.begin succeeded");
    lastDot = 0;
  }
  return true;
}

// Helper: Write OTA upload chunk
static bool otaWriteChunk(AsyncWebServerRequest *request, unsigned char *data, unsigned int len, bool isGz, File &gzFile, size_t &uploaded, unsigned int total, unsigned int index, unsigned int &lastDot) {
  if (isGz) {
    if (!gzFile || gzFile.write(data, len) != len) {
      debugPrintln("[OTA] File write error (gz)");
      otaRespondError(request, "File write error");
      return false;
    }
    uploaded += len;
    if (uploaded % 65536 < len) {
      debugPrint(".");
    }
  } else {
    size_t written = Update.write(data, len);
    if (written != len) {
      debugPrintln("[OTA] Update.write error");
      debugPrint("[OTA] Tried to write: "); debugPrint(len); debugPrint(", actually wrote: "); debugPrintln(written);
      otaRespondError(request, "Update write error");
      return false;
    }
    if (total > 0) {
      unsigned int dot = ((index + len) * 100) / total;
      if (dot != lastDot) {
        debugPrint(".");
        lastDot = dot;
      }
    }
  }
  return true;
}

// Helper: Finalize OTA upload

// Helper: Decompress and flash uploaded gz file, returns true on success, errorMsg set on failure
static bool decompressAndFlashUploadedGz(File &inFile, String &errorMsg) {
  GzUnpacker *GZUnpacker = new GzUnpacker();
  totalBytesWritten = 0;
  updateStarted = false;
  GZUnpacker->setStreamWriter(gzWriteCallback);
  GZUnpacker->setGzProgressCallback([](uint8_t progress) {
    if (webServerPtr)
      webServerPtr->broadcastOtaStatus("progress", "Decompressing", progress);
  });
  bool ok = GZUnpacker->gzStreamExpander(&inFile, inFile.size());
  delete GZUnpacker;
  if (!ok) {
    errorMsg = "Decompression or flash failed";
    return false;
  } else if (!updateStarted) {
    errorMsg = "Update never started - no data written";
    return false;
  } else if (!Update.end(true)) {
    #ifdef ESP32
    errorMsg = String("Update error: ") + Update.getError() + " (" + Update.errorString() + ")";
    #else
    errorMsg = String("Update error: ") + Update.getError() + " (" + Update.getErrorString() + ")";
    #endif
    return false;
  } else if (!Update.isFinished()) {
    errorMsg = "Update not finished properly";
    return false;
  }
  return true;
}

static void otaFinalizeUpload(AsyncWebServerRequest *request, bool isGz, File &gzFile, unsigned int &lastDot) {
  if (lastDot != 0)
    debugPrintln("");
  lastDot = 0;
  AsyncWebServerResponse *resp = nullptr;
  bool ok = false;
  String errorMsg;
  debugPrintln("[OTA] Finalizing upload");
  if (isGz && gzFile) {
    gzFile.close();
    File inFile = LittleFS.open("/ota_upload.bin.gz", "r");
    if (!inFile) {
      errorMsg = "Failed to open uploaded gz file";
      debugPrintln("[OTA] " + errorMsg);
      broadcastOtaStatus("error", errorMsg, -1);
      otaRespondError(request, errorMsg);
      return;
    }
    ok = decompressAndFlashUploadedGz(inFile, errorMsg);
    inFile.close();
    LittleFS.remove("/ota_upload.bin.gz");
    if (!ok) {
      debugPrintln("[OTA] decompressAndFlashUploadedGz failed: " + errorMsg);
      broadcastOtaStatus("error", errorMsg, -1);
    }
  } else {
    ok = Update.end(true);
    if (!ok) {
      #ifdef ESP32
      errorMsg = String("Update error: ") + Update.getError() + " (" + Update.errorString() + ")";
      #else
      errorMsg = String("Update error: ") + Update.getError() + " (" + Update.getErrorString() + ")";
      #endif
      debugPrintln("[OTA] " + errorMsg);
      broadcastOtaStatus("error", errorMsg, -1);
    } else if (!Update.isFinished()) {
      errorMsg = "Update not finished properly";
      ok = false;
      debugPrintln("[OTA] " + errorMsg);
      broadcastOtaStatus("error", errorMsg, -1);
    }
  }
  otaInProgress = false;
  if (ok) {
    // Always send a final 100% progress update before success
    broadcastOtaStatus("progress", String(totalBytesWritten), 100);
    broadcastOtaStatus("success", "OTA update successful", -1);
    resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
  } else {
    String errJson = String("{\"error\":\"") + errorMsg + "\"}";
    debugPrintln("[OTA] OTA failed: " + errorMsg);
    broadcastOtaStatus("error", errorMsg.length() > 0 ? errorMsg : "OTA failed: unknown error", -1);
    resp = request->beginResponse(500, "application/json", errJson);
  }
  for (size_t i = 0; i < 3; ++i)
    resp->addHeader("Access-Control-Allow-Origin", "*");
  request->send(resp);
  if (ok) {
    request->onDisconnect([]() {
      delay(100);
      ESP.restart();
    });
  }
}

void handleOTAUpdate(AsyncWebServerRequest *request, unsigned char *data,
                     unsigned int len, unsigned int index, unsigned int total) {
  static unsigned int lastDot = 0;
  static File gzFile;
  static bool isGz = false;
  static size_t uploaded = 0;
  if (index == 0) {
    if (!otaBeginUpload(request, data, len, total, gzFile, isGz, uploaded, lastDot))
      return;
  }
  if (!otaWriteChunk(request, data, len, isGz, gzFile, uploaded, total, index, lastDot))
    return;
  if (index + len == total) {
    otaFinalizeUpload(request, isGz, gzFile, lastDot);
  }
}

#ifdef ESP32
extern "C" void otaTask(void *parameter) {
  String error;
  bool ok = performGzOtaUpdate(error);
  if (ok) {
    // Wait for OTA ACK from any OTA client, or timeout (max 3s)
    otaAckReceived = false;
    unsigned long waitStart = millis();
    while (millis() - waitStart < 3000) {
      if (otaAckReceived) break;
      yield();
      delay(10);
    }
    if (webServerPtr) {
      webServerPtr->closeOtaClients();
    }
    // Wait for clients to disconnect or timeout (max 2s)
    waitStart = millis();
    while (millis() - waitStart < 2000) {
      if (webServerPtr && webServerPtr->otaClientsConnected() == 0) break;
      yield();
      delay(10);
    }
    debugPrintln("[OTA Task] Rebooting now...");
    ESP.restart();
    delay(5000);
    *((volatile int *)0) = 0; // Force a crash/reboot
  } else {
    debugPrint("[OTA Task] OTA update failed: ");
    debugPrintln(error);
    broadcastOtaStatus("error", error.length() > 0 ? error : "OTA failed: unknown error", -1);
  }
  vTaskDelete(NULL);
}
#endif
