#pragma once

#include <Arduino.h>

#include <ESPAsyncWebServer.h>

extern volatile bool otaInProgress;
extern volatile bool otaRequested;
bool performGzOtaUpdate(String& errorOut);
void setupArduinoOTA(const char* hostname);
void handleArduinoOTA();

class WebServerManager;
void handleOTAUpdate(AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total);

#ifdef ESP32
extern "C" void otaTask(void* parameter = nullptr);
#endif
// Fetch the latest firmware URL for this environment from GitHub (returns empty string on failure)
String getLatestFirmwareUrl(String& latestVersion);
