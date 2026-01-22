#include <Arduino.h>
#ifdef ESP8266
    #include <ESP8266WiFi.h>
    #include <ESPAsyncTCP.h>
#else
    #include <WiFi.h>
    #include <AsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "scheduler.h"

#ifndef WEBSERVER_H
#define WEBSERVER_H

class WebServerManager {
public:
    WebServerManager(Configuration* config, Scheduler* scheduler);
    
    void begin();
    void update();
    void broadcastState();

    // OTA status broadcast
    void broadcastOtaStatus(const String& status, const String& message = "", int progress = -1);
    
    // Callbacks for control actions
    void onPowerChange(void (*callback)(bool));
    void onBrightnessChange(void (*callback)(uint8_t));
    void onEffectChange(void (*callback)(uint8_t, const EffectParams&));
    void onPresetApply(void (*callback)(uint8_t));
    void onConfigChange(void (*callback)());

    // Safety helpers available for other modules
    bool applyBrightnessLimit(uint8_t& brightness);
    bool applyTransitionTimeLimit(uint32_t& transitionTime);
    
private:
    Configuration* _config;
    Scheduler* _scheduler;
    AsyncWebServer* _server;
    AsyncWebSocket* _ws;
    
    uint32_t _lastBroadcast = 0;
    
    // Callbacks
    void (*_powerCallback)(bool) = nullptr;
    void (*_brightnessCallback)(uint8_t) = nullptr;
    void (*_effectCallback)(uint8_t, const EffectParams&) = nullptr;
    void (*_presetCallback)(uint8_t) = nullptr;
    void (*_configCallback)() = nullptr;
    
    // Setup handlers
    void setupRoutes();
    void setupWebSocket();
    
    // REST API handlers
    // API/page/ws handlers convert percent<->hex at boundaries
    void handleGetState(AsyncWebServerRequest* request); // convert hex->percent for user
    void handleSetState(AsyncWebServerRequest* request, uint8_t* data, size_t len); // convert percent->hex from user
    void handleGetPresets(AsyncWebServerRequest* request);
    void handleSetPreset(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleGetConfig(AsyncWebServerRequest* request); // convert hex->percent for user
    void handleSetConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len); // convert percent->hex from user
    void handleGetTimers(AsyncWebServerRequest* request);
    void handleSetTimer(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    
    // Helper functions
    String getStateJSON();
    String getPresetsJSON();
    String getConfigJSON();
    String getTimersJSON();
        friend bool performGzOtaUpdate(String& errorOut);
        friend void otaProgressCallback(uint8_t progress);
};

#endif
