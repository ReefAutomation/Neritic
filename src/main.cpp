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
// #include "effects.h" // Custom effects disabled, using WS2812FX native effects only
#include "scheduler.h"
#include "transition.h"
#include "webserver.h"
#include "captive_portal.h"
#include <Arduino.h>
#include "debug.h"
#include "ota.h"

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
void applyPreset(uint8_t presetId);
void setPower(bool power);
void setBrightness(uint8_t brightness);
void setEffect(uint8_t effect, const EffectParams& params);
void updateLEDs();
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

    // Load configuration
    if (!config.load()) {
        config.setDefaults();
        config.save();
    }
    // Ensure lastConfiguration matches loaded config at boot
    lastConfiguration = config;

    // Load presets
    if (!config.loadPresets()) {
        debugPrintln("Failed to load presets");
        config.savePresets();
    }

    // Initialize LEDs
    setupLEDs();

    // Initialize transition engine brightness to default
    extern TransitionEngine transition;
    transition.forceCurrentBrightness(config.state.brightness); // Set current
    transition.startTransition(config.state.brightness, 1);     // Set target

    // Connect to WiFi

    setupWiFi();
    printHeap("[DEBUG] After WiFi connect");
    delay(500); // Give network stack time to settle

    // Setup web server callbacks (moved up)
    webServer.onPowerChange(setPower);
    webServer.onBrightnessChange(setBrightness);
    webServer.onEffectChange(setEffect);
    webServer.onPresetApply(applyPreset);
    webServer.onConfigChange([]() {
        // Immediately apply relay pin and logic changes
        pinMode(config.led.relayPin, OUTPUT);
        digitalWrite(config.led.relayPin, config.state.power ? (config.led.relayActiveHigh ? HIGH : LOW) : (config.led.relayActiveHigh ? LOW : HIGH));
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
            setEffect(config.state.effect, config.state.params);
            setBrightness(config.state.brightness);
            setPower(config.state.power);
        }
    });

    // Start web server (moved up)
    debugPrintln("[DEBUG] Starting web server...");
    printHeap("[DEBUG] Before webServer.begin");
    webServer.begin();
    debugPrintln("[DEBUG] Web server started");
    printHeap("[DEBUG] After webServer.begin");

    // Initialize scheduler
    scheduler.begin();
    debugPrintln("[DEBUG] Scheduler initialized");

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
    int8_t bootPreset = scheduler.getBootPreset();
    if (bootPreset >= 0 && bootPreset < MAX_PRESETS) {
        applyPreset(bootPreset);
    } else {
        // Ensure transition starts from the actual brightness, not 0
        transition.forceCurrentBrightness(config.state.brightness);
        setEffect(config.state.effect, config.state.params);
        setBrightness(config.state.brightness);
        setPower(config.state.power);
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
    checkAndApplyScheduleAfterBoot();
    // Handle ArduinoOTA (ESP32 only)
    handleArduinoOTA();
    // Update all systems
    scheduler.update();
    webServer.update();
    transition.update();
    
    // Check schedule every loop (scheduler has internal rate limiting)
    checkSchedule();
    
    // Update LEDs at target frame rate (manual timing)
    static uint32_t lastFrame = 0;
    uint32_t now = millis();
    if (now - lastFrame >= (1000 / FRAMES_PER_SECOND)) {
        lastFrame = now;
        updateLEDs();
    }

    // Captive portal DNS handler (if in AP mode)
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

void applyPreset(uint8_t presetId) {
    if (presetId >= MAX_PRESETS || !config.presets[presetId].enabled) {
        debugPrintln("Invalid preset ID");
        return;
    }
    Preset& preset = config.presets[presetId];
    // Get timer brightness percent if available
    uint8_t timerBrightnessPercent = 100;
    // Find active timer for this preset
    for (size_t i = 0; i < config.timers.size(); i++) {
        if (config.timers[i].presetId == presetId && config.timers[i].enabled) {
            timerBrightnessPercent = config.timers[i].brightness;
            break;
        }
    }
    // Convert percent to 0-255
    uint8_t brightnessValue = (uint8_t)((timerBrightnessPercent / 100.0) * 255);
    // Apply safety limits
    uint8_t safeBrightness = min(brightnessValue, config.safety.maxBrightness);
    // Start transitions
    uint32_t transTime = config.state.transitionTime;
    if (transTime < config.safety.minTransitionTime) {
        transTime = config.safety.minTransitionTime;
    }
    transition.startTransition(safeBrightness, transTime);
    setEffect(preset.effect, preset.params);
    config.state.currentPreset = presetId;
    config.state.power = true;
    config.state.inTransition = true;
    webServer.broadcastState();
}

void setPower(bool power) {
    if (config.state.power == power) {
        return;
    }
    config.state.power = power;
    // Control relay
    digitalWrite(config.led.relayPin, power ? (config.led.relayActiveHigh ? HIGH : LOW) : (config.led.relayActiveHigh ? LOW : HIGH));
    uint8_t targetBrightness = power ? config.state.brightness : 0;
    uint32_t transTime = config.state.transitionTime;
    if (transTime < config.safety.minTransitionTime) {
        transTime = config.safety.minTransitionTime;
    }
    if (power) {
        transition.forceCurrentBrightness(config.state.brightness);
    }
    if (transition.getCurrentBrightness() != targetBrightness || !transition.isTransitioning()) {
        transition.startTransition(targetBrightness, transTime);
    }
    webServer.broadcastState();
}

void setBrightness(uint8_t brightness) {
    brightness = min(brightness, config.safety.maxBrightness);
    uint32_t transTime = config.state.transitionTime;
    if (transTime < config.safety.minTransitionTime) {
        transTime = config.safety.minTransitionTime;
    }
    uint8_t current = transition.getCurrentBrightness();
    if (brightness != current) {
        if (!transition.isTransitioning()) {
            transition.forceCurrentBrightness(config.state.brightness);
        }
        transition.startTransition(brightness, transTime);
        webServer.broadcastState();
    }
}

void setEffect(uint8_t effect, const EffectParams& params) {
    config.state.effect = effect;
    config.state.params = params;
    // Set WS2812FX effect and color
    if (strip) {
        strip->setMode(effect);
        strip->setColor(params.color1);
    }
    webServer.broadcastState();
}

void updateLEDs() {
    // Only update brightness and call WS2812FX service
    if (!config.state.power) {
        strip->setBrightness(0);
        strip->service();
        config.state.inTransition = false;
        config.state.brightness = 0;
        // Power off relay when LEDs are off
        digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? LOW : HIGH); // Relay off
        return;
    }
    extern TransitionEngine transition;
    uint8_t currentBrightness = transition.getCurrentBrightness();
    uint8_t prevBrightness = config.state.brightness;
    strip->setBrightness(currentBrightness);
    strip->service();
    config.state.inTransition = false;
    config.state.brightness = currentBrightness;
    // Power on relay when brightness is above 0
    digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? HIGH : LOW); // Relay on
}

void checkSchedule() {
    int8_t presetId = scheduler.getBootPreset();
    if (presetId >= 0 && presetId < MAX_PRESETS) {
        if (presetId != config.state.currentPreset) {
            applyPreset(presetId);
        }
    }
}


// Apply the correct schedule as soon as time becomes valid after boot (only once)
void checkAndApplyScheduleAfterBoot() {
    static bool scheduleApplied = false;
    if (!scheduleApplied) {
        if (scheduler.isTimeValid()) {
            int8_t bootPreset2 = scheduler.getBootPreset();
            if (bootPreset2 >= 0 && bootPreset2 < MAX_PRESETS) {
                applyPreset(bootPreset2);
            }
            scheduleApplied = true; 
        }
    }
}
