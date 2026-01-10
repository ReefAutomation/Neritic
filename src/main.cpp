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
#include <WS2812FX.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <LittleFS.h>
#define FILESYSTEM LittleFS

#include "config.h"
#include "presets.h"
// #include "effects.h" // Custom effects disabled, using WS2812FX native effects only
#include "scheduler.h"
#include "transition.h"
#include "webserver.h"
#include "captive_portal.h"
#include <Arduino.h>
#include "debug.h"
#include "ota.h"
#include "config.h"
#include "state.h"

#include "display.h"

// Track last configuration for change detection
Configuration lastConfiguration;

// Global objects
Configuration config;
Scheduler scheduler(&config);
TransitionEngine transition;
WebServerManager webServer(&config, &scheduler);

// LED array
#include <type_traits>

// WS2812FX LED object
WS2812FX* strip = nullptr;

// Timing
uint32_t lastStateSave = 0;
uint32_t lastUpdate = 0;

// Track last timers for schedule update
std::vector<Timer> lastTimers;

// Track last scheduled preset applied by timer
int8_t lastScheduledPreset = -1;

// Function declarations
void setupWiFi();
void printHeap(const char* tag) {
    #if defined(ESP32)
    debugPrint(tag);
    debugPrint(F(" Heap: "));
    debugPrintln(ESP.getFreeHeap());
    #endif
}
void setupLEDs();
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

    // Initialize LEDs
    setupLEDs();

    // Initialize transition engine brightness to default
    extern TransitionEngine transition;
    transition.forceCurrentBrightness(state.brightness); // Set current
    transition.startTransition(state.brightness, 1);     // Set target

    // Connect to WiFi

    setupWiFi();
    printHeap("[DEBUG] After WiFi connect");
    delay(500); // Give network stack time to settle

    // Setup web server callbacks (moved up)
    webServer.onPowerChange(setPower);
    webServer.onBrightnessChange(setBrightness);
    webServer.onEffectChange(setEffect);
    webServer.onPresetApply([](uint8_t presetId) { applyPreset(presetId, false); });
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

    // Check if we should apply a scheduled preset on boot
    int8_t bootPreset = scheduler.getCurrentScheduledPreset();
    if (bootPreset >= 0 && bootPreset < config.getPresetCount()) {
        applyPreset(bootPreset);
    } else {
        // Ensure transition starts from the actual brightness, not 0
        transition.forceCurrentBrightness(state.brightness);
        setEffect(state.effect, state.params);
        setBrightness(state.brightness);
        setPower(state.power);
    }

    // Mark that we have not yet applied a schedule after time becomes valid
    // (Handled in loop by checkAndApplyScheduleAfterBoot)
    debugPrintln();
    debugPrintln("System ready!");
    debugPrint("IP Address: ");
    debugPrintln(WiFi.localIP());
    debugPrintln("=================================");
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
        if (state.currentPreset < config.getPresetCount()) {
            presetName = config.presets[state.currentPreset].name;
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
    debugPrint("Connecting to WiFi");
    // Set hostname
    #ifdef ESP8266
        WiFi.hostname(config.network.hostname);
    #else
        WiFi.setHostname(config.network.hostname.c_str());
    #endif
    // Connect to WiFi
    if (config.network.ssid.length() > 0) {
        WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
        int attempts = 0;
        int maxAttempts = 60; // 60 x 500ms = 30s (double previous)
        while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
            delay(500);
            debugPrint(".");
            attempts++;
        }
        // If not connected, try a second round of retries (total up to 60s)
        if (WiFi.status() != WL_CONNECTED) {
            debugPrintln();
            debugPrintln("First WiFi attempt failed, retrying...");
            WiFi.disconnect();
            delay(1000);
            WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
            attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
                delay(500);
                debugPrint(".");
                Serial.print("[DEBUG] WiFi.status(): ");
                Serial.println(WiFi.status());
                attempts++;
            }
        }
        if (WiFi.status() == WL_CONNECTED) {
            debugPrintln();
            debugPrint("Connected! IP: ");
            debugPrintln(WiFi.localIP());
            stopCaptivePortal();
            return;
        }
    }
    // If connection failed or no credentials, start AP mode
    debugPrintln();
    debugPrintln("Starting Access Point mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
    debugPrint("AP IP: ");
    debugPrintln(WiFi.softAPIP());
    // Start captive portal DNS
    startCaptivePortal(WiFi.softAPIP());
}

void setupLEDs() {
    if (strip) {
        delete strip;
        strip = nullptr;
    }
    uint8_t pin = config.led.pin;
    uint16_t count = config.led.count;
    String type = config.led.type;
    String order = config.led.colorOrder;
    type.toUpperCase();
    order.toUpperCase();
    uint8_t wsType = NEO_GRB + NEO_KHZ800; // default
    if (type.indexOf("SK6812") >= 0) {
        if (order == "RGBW") wsType = NEO_RGBW + NEO_KHZ800;
        else if (order == "GRBW") wsType = NEO_GRBW + NEO_KHZ800;
        else wsType = NEO_GRBW + NEO_KHZ800; // default for SK6812
    } else if (type.indexOf("WS2812") >= 0) {
        if (order == "RGB") wsType = NEO_RGB + NEO_KHZ800;
        else if (order == "GRB") wsType = NEO_GRB + NEO_KHZ800;
        else wsType = NEO_GRB + NEO_KHZ800; // default for WS2812
    } else if (type.indexOf("APA106") >= 0) {
        wsType = NEO_RGB + NEO_KHZ800;
    }
    strip = new WS2812FX(count, pin, wsType);
    strip->init();
    // Set default effect: solid black (STATIC, color 0x000000)
    strip->setMode(0); // 0 = STATIC
    strip->setColor(0x000000); // black
    strip->setBrightness(0); // off
    strip->show();
    strip->start();
}


void checkSchedule() {
    // Always apply the most recent valid scheduled preset for the current time
    int8_t scheduledPresetId = scheduler.getCurrentScheduledPreset();
    if (scheduledPresetId >= 0 && scheduledPresetId < config.getPresetCount()) {
        if (scheduledPresetId != lastScheduledPreset) {
            applyPreset(scheduledPresetId, false);
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
            if (bootPreset2 >= 0 && bootPreset2 < config.getPresetCount()) {
                applyPreset(bootPreset2, false);
                lastScheduledPreset = bootPreset2;
            }
            scheduleApplied = true; 
        }
    }
}
