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
#include "network.h"
#include <LittleFS.h>
#define FILESYSTEM LittleFS
#include <type_traits>

#include "bus_manager.h"
#include "config.h"
#include "debug.h"
#include "effects.h"
#include "ota.h"
#include "presets.h"
#include "scheduler.h"
#include "state.h"
#include "transition.h"
#include "webserver.h"
#include <Arduino.h>

#include "display.h"
#include "inc/version.inc"
#include "inc/version_def.inc"

// Global BusManager instance
BusManager busManager;
WebServerManager *webServerPtr = nullptr;

// Track last configuration for change detection
Configuration lastConfiguration;

// Global objects
Configuration config;
Scheduler scheduler(&config);
TransitionEngine transition;
WebServerManager webServer(&config, &scheduler);

// Use void* for runtime type switching
void *strip = nullptr;

// Timing
uint32_t lastStateSave = 0;
uint32_t lastUpdate = 0;

// Track last timers for schedule update
std::vector<Timer> lastTimers;

// Track last scheduled preset applied by timer
int8_t lastScheduledPreset = -1;

extern TransitionEngine transition;
static bool apFallbackTriggered = false;

// Function declarations
void setupLEDs();
void addBusToManager();
void checkSchedule();
void checkAndApplyScheduleAfterBoot();

void setup() {
  Serial.begin(SERIAL_BAUD);
  webServerPtr = &webServer;
  // Load configuration
  if (!config.load()) {
    config.setDefaults();
    config.save();
  }
  lastConfiguration = config;

  // Load presets
  if (!loadPresets(config.presets)) {
    debugPrintln("Failed to load presets");
    savePresets(config.presets);
  }

  // Initialize LEDs and BusManager
  setupLEDs();
  updatePixelCount();
  busManager.turnOffLEDs();

  // Initialize relay pin from config
  pinMode(config.led.relayPin, OUTPUT);
  digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? LOW : HIGH);

  // Initialize transition engine brightness to default
  transition.forceCurrentBrightness(state.brightness);

  delay(1000);
  debugPrintln();
  debugPrintln("=================================");
  debugPrintln("  Aquarium LED Controller");
  debugPrintln("  Version: %s", FW_VERSION);
  debugPrintln("=================================");

  // List files in LittleFS for debugging
  LittleFS.begin();

  // Initialize display (test)
  setup_display();

  // Connect to WiFi
  networkSetup(config);
  delay(500);

  // Setup web server callbacks
  webServer.onPowerChange(setPower);
  webServer.onBrightnessChange(setBrightness);
  webServer.onEffectChange(setEffect);
  webServer.onPresetApply([](uint8_t presetId) {
    applyPreset(presetId, transition._targetState.brightness);
  });
  webServer.onConfigChange([]() {
    pinMode(config.led.relayPin, OUTPUT);
    digitalWrite(config.led.relayPin,
                 state.power ? (config.led.relayActiveHigh ? HIGH : LOW)
                             : (config.led.relayActiveHigh ? LOW : HIGH));
    bool locationChanged =
        config.time.latitude != lastConfiguration.time.latitude ||
        config.time.longitude != lastConfiguration.time.longitude;
    if (locationChanged) {
      scheduler.calculateSunTimes();
      lastConfiguration.time.latitude = config.time.latitude;
      lastConfiguration.time.longitude = config.time.longitude;
    }
    bool timersChanged = config.timers != lastConfiguration.timers;
    if (timersChanged) {
      scheduler.begin();
      lastConfiguration.timers = config.timers;
    }
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
    if (ledChanged) {
      transition._previousState = transition._currentState;
      transition = TransitionEngine();
      transition.startTransition(transition._previousState, 0);
      updateLEDs();
      setEffect(state.effect, state.params);
      setBrightness(state.brightness);
      setPower(state.power);
    }
  });

  webServer.begin();
  scheduler.begin();
  setupArduinoOTA(config.network.hostname.c_str());

  // Only wait for time sync if connected to WiFi (STA mode)
  if (WiFi.getMode() == WIFI_STA && WiFi.isConnected()) {
    debugPrintln("Waiting for time sync...");
    for (int i = 0; i < 30; i++) {
      scheduler.update();
      if (scheduler.isTimeValid()) {
        debugPrintln("Time synchronized!");
        break;
      }
      delay(1000);
    }
  } else {
    debugPrintln("Skipping time sync (AP mode)");
    // Scan for WiFi networks and print SSIDs in AP mode for captive portal
    debugPrintln("Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
      debugPrintln("No networks found");
    } else {
      debugPrintln("Networks found:");
      for (int i = 0; i < n; ++i) {
        debugPrint("  ");
        debugPrintln(WiFi.SSID(i));
      }
    }
  }

  debugPrintln();
  debugPrintln("System ready!");
  debugPrint("IP Address: ");
  debugPrintln(getCurrentIpString(config));
  debugPrintln("=================================");

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
  // Only run schedule logic if time is valid
  if (scheduler.isTimeValid()) {
    checkAndApplyScheduleAfterBoot();
    // Only check schedule on a new round minute
    static int lastCheckedMinute = -1;
    int currentMinute = scheduler.getCurrentMinute();
    if (currentMinute != lastCheckedMinute) {
      checkSchedule();
      lastCheckedMinute = currentMinute;
    }
  }
  handleArduinoOTA();
  scheduler.update();
  webServer.update();
  transition.update();
  networkLoop(config);
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
    String ipStr = getCurrentIpString(config);
    if (presetName != lastPreset || state.power != lastPower ||
        state.brightness != lastBrightness || ipStr != lastIp) {
      display_status(presetName.c_str(), state.power, ipStr.c_str());
      lastPreset = presetName;
      lastPower = state.power;
      lastBrightness = state.brightness;
      lastIp = ipStr;
    }
  }
}

void setupLEDs() {
  busManager.setupStrip(config.led.type, config.led.colorOrder, config.led.pin,
                        config.led.count);
}

void handleScheduledPreset(int8_t presetId, int currentMinutes) {
  const Timer *activeTimer = scheduler.getActiveTimer();
  if (activeTimer && activeTimer->presetId == presetId &&
      presetId != lastScheduledPreset) {
    uint8_t brightness = activeTimer->brightness;
    // If this is the first schedule application after boot, use powerOn
    // transition time
    static bool firstScheduleApplied = false;
    uint32_t transitionTime = firstScheduleApplied
                                  ? config.transitionTimes.schedule
                                  : config.transitionTimes.powerOn;
    webServer.applyTransitionTimeLimit(transitionTime);
    state.transitionTime = transitionTime;
    applyPreset(presetId, brightness);
    firstScheduleApplied = true;
    lastScheduledPreset = presetId;
  }
}

void checkSchedule() {
  const Timer *activeTimer = scheduler.getActiveTimer();
  if (activeTimer) {
    handleScheduledPreset(activeTimer->presetId,
                          scheduler.getTimerMinutes(*activeTimer));
  }
}

// Apply the correct schedule as soon as time becomes valid after boot (only
// once)

void checkAndApplyScheduleAfterBoot() {
  static bool scheduleApplied = false;
  if (!scheduleApplied) {
    if (scheduler.isTimeValid()) {
      checkSchedule();
      scheduleApplied = true;
    }
  }
}
