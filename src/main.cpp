/*
 * Standalone Aquarium LED Controller
 * ESP32/ESP8266 Fish-Safe LED Controller with Scheduling
 * 
 * Features:
 * - 6 Custom aquarium effects
 * - Fish-safe transitions and brightness limits
 * - NTP time sync with sunrise/sunset calculation
 * - Advanced scheduling system with boot recovery
 * - Modern web interface with WebSocket updates
 * - Preset management
 */

#include <Arduino.h>
#include <memory>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <LittleFS.h>
#define FILESYSTEM LittleFS
#include <type_traits>

#include "config.h"
#include "presets.h"
#include "effects.h"
#include "scheduler.h"
#include "bus_manager.h"
#include "transition.h"
#include "webserver.h"
#include "captive_portal.h"
#include <Arduino.h>
#include "debug.h"
#include "ota.h"
#include "config.h"
#include "state.h"

#include "display.h"


// Global BusManager instance
BusManager busManager;

// Track last configuration for change detection
Configuration lastConfiguration;

// Global objects
Configuration config;
Scheduler scheduler(&config);
TransitionEngine transition;
WebServerManager webServer(&config, &scheduler);

// Use void* for runtime type switching
void* strip = nullptr;

// Timing
uint32_t lastStateSave = 0;
uint32_t lastUpdate = 0;

// Track last timers for schedule update
std::vector<Timer> lastTimers;

// Track last scheduled preset applied by timer
int8_t lastScheduledPreset = -1;

// Function declarations
void setupWiFi();
void setupLEDs();
void addBusToManager();
void checkSchedule();
void checkAndApplyScheduleAfterBoot();

void setup() {
    // Initialize relay pin from config
    pinMode(config.led.relayPin, OUTPUT);
    // Set relay to off state at boot
    digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? LOW : HIGH);
    #ifdef DEBUG_SERIAL
    Serial.begin(115200);
    delay(1000);
    #endif
    debugPrintln();
    debugPrintln("=================================");
    debugPrintln("  Aquarium LED Controller v1.0  ");
    debugPrintln("=================================");
    
    // List files in LittleFS for debugging
    LittleFS.begin();

    // Initialize display (test)
    setup_display();

    // Load configuration
    if (!config.load()) {
        config.setDefaults();
        config.save();
    }
    // Ensure lastConfiguration matches loaded config at boot
    lastConfiguration = config;

    // Load presets
    if (!loadPresets(config.presets)) {
        debugPrintln("Failed to load presets");
        savePresets(config.presets);
    }


    // Initialize LEDs and BusManager
    setupLEDs();
    updatePixelCount();

    // Initialize transition engine brightness to default
    extern TransitionEngine transition;
    transition.forceCurrentBrightness(state.brightness); // Set current
    // Removed unnecessary initial transition at boot

    // Connect to WiFi

    setupWiFi();
    delay(500); // Give network stack time to settle

    // Setup web server callbacks (moved up)
    webServer.onPowerChange(setPower);
    webServer.onBrightnessChange(setBrightness);
    webServer.onEffectChange(setEffect);
    webServer.onPresetApply([](uint8_t presetId) { applyPreset(presetId); });
    webServer.onConfigChange([]() {
        // Immediately apply relay pin and logic changes
        pinMode(config.led.relayPin, OUTPUT);
        digitalWrite(config.led.relayPin, state.power ? (config.led.relayActiveHigh ? HIGH : LOW) : (config.led.relayActiveHigh ? LOW : HIGH));
        // Only recalculate sun times if location changed
        bool locationChanged = config.time.latitude != lastConfiguration.time.latitude || config.time.longitude != lastConfiguration.time.longitude;
        if (locationChanged) {
            scheduler.calculateSunTimes();
            lastConfiguration.time.latitude = config.time.latitude;
            lastConfiguration.time.longitude = config.time.longitude;
        }
        // Only update schedule if timers changed
        bool timersChanged = config.timers != lastConfiguration.timers;
        if (timersChanged) {
            scheduler.begin(); // or scheduler.update() if begin is too heavy
            lastConfiguration.timers = config.timers;
        }
        // Only reinitialize LEDs if hardware config changed
        bool ledChanged = config.led.pin != lastConfiguration.led.pin ||
                          config.led.count != lastConfiguration.led.count ||
                          config.led.type != lastConfiguration.led.type ||
                          config.led.colorOrder != lastConfiguration.led.colorOrder;
        if (ledChanged) {
            setupLEDs();
            updatePixelCount();
            lastConfiguration.led.pin = config.led.pin;
            lastConfiguration.led.count = config.led.count;
            lastConfiguration.led.type = config.led.type;
            lastConfiguration.led.colorOrder = config.led.colorOrder;
        }
        // Only reset transition engine and update LEDs if LED config changed
        if (ledChanged) {
            uint8_t prevBrightness = transition.getCurrentBrightness();
            uint32_t prevColor1 = transition.getCurrentColor1();
            uint32_t prevColor2 = transition.getCurrentColor2();
            transition = TransitionEngine();
            transition.startTransition(prevBrightness, 0);
            transition.startColorTransition(prevColor1, prevColor2, 0);
            updateLEDs();
            // Restore effect, brightness, and power after reinitializing LEDs
            setEffect(state.effect, state.params);
            setBrightness(state.brightness);
            setPower(state.power);
        }
    });

    // Start web server
    webServer.begin();

    // Initialize scheduler
    scheduler.begin();

    // Setup ArduinoOTA (ESP32 only)
    setupArduinoOTA(config.network.hostname.c_str());
    
    // Wait for NTP time sync
    debugPrintln("Waiting for time sync...");
    for (int i = 0; i < 30; i++) {
        scheduler.update();
        if (scheduler.isTimeValid()) {
            debugPrintln("Time synchronized!");
            break;
        }
        delay(1000);
    }

    debugPrintln();
    debugPrintln("System ready!");
    debugPrint("IP Address: ");
    debugPrintln(WiFi.localIP());
    debugPrintln("=================================");

    // Do not apply a scheduled preset here; let checkAndApplyScheduleAfterBoot() handle it after time sync
    // Ensure transition starts from the actual brightness, not 0
    transition.forceCurrentBrightness(state.brightness);
    setEffect(state.effect, state.params);
    setBrightness(state.brightness);
    setPower(state.power);

}

void loop() {
    // Prioritize OTA: if OTA is in progress, only handle OTA and show debug dots
    if (otaInProgress) {
        handleArduinoOTA();
        // Show debug dots handled in OTA progress callback
        return;
    }
    checkAndApplyScheduleAfterBoot();
    handleArduinoOTA();
    scheduler.update();
    webServer.update();
    transition.update();
    checkSchedule();
    static uint32_t lastFrame = 0;
    uint32_t now = millis();
    if (now - lastFrame >= (1000 / FRAMES_PER_SECOND)) {
        lastFrame = now;
        updateLEDs();
        // Only update display if status changes
        static String lastPreset;
        static bool lastPower = false;
        static uint8_t lastBrightness = 0;
        static String lastIp;
        String presetName = "-";
        if (state.preset < config.getPresetCount()) {
            presetName = config.presets[state.preset].name;
        }
        String ipStr = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        if (presetName != lastPreset || state.power != lastPower || state.brightness != lastBrightness || ipStr != lastIp) {
            display_status(presetName.c_str(), state.power, state.brightness, ipStr.c_str());
            lastPreset = presetName;
            lastPower = state.power;
            lastBrightness = state.brightness;
            lastIp = ipStr;
        }
    }
    if (WiFi.getMode() == WIFI_AP) {
        handleCaptivePortalDns();
    }
}

void setupWiFi() {
    debugPrintln("[WiFi] setupWiFi() called");
    debugPrint("Connecting to WiFi");
    // Set hostname
    #ifdef ESP8266
        WiFi.hostname(config.network.hostname);
    #else
        WiFi.setHostname(config.network.hostname.c_str());
    #endif
    // Connect to WiFi
    if (config.network.ssid.length() > 0) {
        debugPrintln("");
        debugPrintln("[WiFi] Calling WiFi.begin");
        WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
        int attempts = 0;
        int maxAttempts = 60; // 60 x 500ms = 30s (double previous)
        while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
            delay(500);
            debugPrint(".");
            attempts++;
        }
        debugPrintln("");
        debugPrintln("[WiFi] First connection attempt done");
        // If not connected, try a second round of retries (total up to 60s)
        if (WiFi.status() != WL_CONNECTED) {
            debugPrintln("");
            debugPrintln("[WiFi] First WiFi attempt failed, retrying...");
            WiFi.disconnect();
            delay(1000);
            debugPrintln("[WiFi] Calling WiFi.begin (retry)");
            WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
            attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
                delay(500);
                debugPrint(".");
                attempts++;
            }
            debugPrintln("");
            debugPrintln("[WiFi] Second connection attempt done");
        }
        if (WiFi.status() == WL_CONNECTED) {
            debugPrintln("");
            debugPrintln("[WiFi] Connected!");
            debugPrint("Connected! IP: ");
            debugPrintln(WiFi.localIP());
            stopCaptivePortal();
            return;
        }
    }
    // If connection failed or no credentials, start AP mode
    debugPrintln("");
    debugPrintln("[WiFi] Starting Access Point mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
    debugPrint("AP IP: ");
    debugPrintln(WiFi.softAPIP());
    // Start captive portal DNS
    startCaptivePortal(WiFi.softAPIP());
}

void setupLEDs() {
    busManager.setupStrip(config.led.type, config.led.colorOrder, config.led.pin, config.led.count);
}


void checkSchedule() {
    // Always apply the most recent valid scheduled preset for the current time
    int8_t scheduledPresetId = scheduler.getCurrentScheduledPreset();
    auto it = std::find_if(config.presets.begin(), config.presets.end(), [scheduledPresetId](const Preset& p) { return p.id == scheduledPresetId; });
    if (it != config.presets.end()) {
        if (scheduledPresetId != lastScheduledPreset) {
            applyPreset(scheduledPresetId);
            lastScheduledPreset = scheduledPresetId;
        }
    }
}


// Apply the correct schedule as soon as time becomes valid after boot (only once)
void checkAndApplyScheduleAfterBoot() {
    static bool scheduleApplied = false;
    if (!scheduleApplied) {
        if (scheduler.isTimeValid()) {
            int8_t bootPreset2 = scheduler.getCurrentScheduledPreset();
            auto it = std::find_if(config.presets.begin(), config.presets.end(), [bootPreset2](const Preset& p) { return p.id == bootPreset2; });
            if (it != config.presets.end()) {
                applyPreset(bootPreset2);
                lastScheduledPreset = bootPreset2;
            }
            scheduleApplied = true; 
        }
    }
}
