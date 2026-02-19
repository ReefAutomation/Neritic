/*
 * Standalone Aquarium LED Controller
 * ESP32 Fish-Safe LED Controller with Scheduling
 *
 * Features:
 * - 6 Custom aquarium effects
 * - Fish-safe transitions and brightness limits
 * - NTP time sync with sunrise/sunset calculation
 * - Advanced scheduling system with boot recovery
 * - Modern web interface with WebSocket updates
 * - Preset management
 */

#include "bus_manager.h"
#include "config.h"
#include "driver/gpio.h"
#include "effects.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "network.h"
#include "nvs_flash.h"
#include "ota.h"
#include "presets.h"
#include "scheduler.h"
#include "state.h"
#include "transition.h"
#include "webserver.h"
#include <string.h>

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

// Function declarations
void setupLEDs();
void addBusToManager();
void checkSchedule();
void checkAndApplyScheduleAfterBoot();
extern "C" void main_task(void *pvParameters);

// ESP-IDF: Replace setup() with app_main()
extern "C" void app_main() {
  // app_main has a small stack — do nothing heavy here.
  // Just delay for USB enumeration, then hand off to main_task.
  vTaskDelay(pdMS_TO_TICKS(3000));
  xTaskCreate(main_task, "main_task", 32768, NULL, 5, NULL);
}

void main_task(void *pvParameters) {
  esp_log_level_set("main", ESP_LOG_INFO);
  // Expected when browser/WebSocket clients disconnect abruptly.
  // Keep internal httpd_ws noise low while preserving app-level logs.
  esp_log_level_set("httpd_ws", ESP_LOG_ERROR);
#ifdef DEBUG_SERIAL
  esp_log_level_set("transition", ESP_LOG_DEBUG);
  esp_log_level_set("state", ESP_LOG_DEBUG);
#endif
  ESP_LOGI("main", "Aquarium LED Controller starting... stack=%u",
           (unsigned)uxTaskGetStackHighWaterMark(NULL));

  // NVS must be initialized before WiFi
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_err);

  webServerPtr = &webServer;
  ESP_LOGI("main", "step: config.load");

  // Load configuration
  if (!config.load()) {
    config.setDefaults();
    config.save();
  }
  lastConfiguration = config;
  ESP_LOGI("main", "step: presets");

  // Load presets
  if (!loadPresets(config.presets)) {
    ESP_LOGW("main", "Failed to load presets");
    savePresets(config.presets);
  }
  ESP_LOGI("main", "step: LEDs pin=%d count=%d type=%s order=%s",
           config.led.pin, config.led.count, config.led.type.c_str(),
           config.led.colorOrder.c_str());

  // Guard: skip LED init if config is invalid
  if (config.led.count > 0 && config.led.count <= 512 && config.led.pin < 22) {
    setupLEDs();
    updatePixelCount();
    busManager.turnOffLEDs();
  } else {
    ESP_LOGW("main", "Skipping LED init: invalid config (count=%d pin=%d)",
             config.led.count, config.led.pin);
  }

  // Initialize relay pin from config
  gpio_set_direction((gpio_num_t)config.led.relayPin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)config.led.relayPin,
                 config.led.relayActiveHigh ? 0 : 1);

  // Initialize transition engine brightness to default
  transition.forceCurrentBrightness(state.brightness);
  ESP_LOGI("main", "step: network");

  // Initialize display
#ifdef DISPLAY_ENABLED
  setup_display();
#endif

  // Connect to WiFi
  networkSetup(config);
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI("main", "step: webserver setup");

  // Setup web server callbacks
  webServer.onPowerChange(setPower);
  webServer.onBrightnessChange(setBrightness);
  webServer.onEffectChange(setEffect);
  webServer.onPresetApply([](uint8_t presetId) {
    applyPreset(presetId, transition._targetState.brightness);
  });
  webServer.onConfigChange([]() {
    gpio_set_direction((gpio_num_t)config.led.relayPin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)config.led.relayPin,
                   state.power ? (config.led.relayActiveHigh ? 1 : 0)
                               : (config.led.relayActiveHigh ? 0 : 1));
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
      lastConfiguration.timers = config.timers;
    }
    bool ntpServerChanged =
        config.time.ntpServer != lastConfiguration.time.ntpServer;
    if (ntpServerChanged) {
      scheduler.updateNTP();
      lastConfiguration.time.ntpServer = config.time.ntpServer;
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

    // --- Apply transition if maxBrightness changed ---
    if (config.safety.maxBrightness != lastConfiguration.safety.maxBrightness) {
      uint8_t targetBrightness = state.brightness;
      if (state.brightness > config.safety.maxBrightness) {
        targetBrightness = config.safety.maxBrightness;
      }
      setBrightness(targetBrightness);
      lastConfiguration.safety.maxBrightness = config.safety.maxBrightness;
    }
  });

  webServer.begin();
  scheduler.begin();
  setupArduinoOTA(config.network.hostname.c_str());

  enum NetworkReadyState {
    NET_READY_NONE = 0,
    NET_READY_AP,
    NET_READY_STA,
  };
  NetworkReadyState lastNetReadyState = NET_READY_NONE;
  bool lastStaConnectedForNtp = false;

  // Give networkLoop() a chance to activate AP fallback if WiFi failed during
  // startup Process it several times to ensure AP mode is activated before we
  // check mode below
  for (int i = 0; i < 5; i++) {
    networkLoop(config);
    scheduler.update();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Only wait for time sync if NOT in AP mode (i.e., in STA mode)
  // Check mode instead of IP connection to avoid race condition where IP hasn't
  // been assigned yet
  bool in_ap_mode = networkIsApMode();
  if (!in_ap_mode) {
    ESP_LOGI("main", "Waiting for time sync...");
    for (int i = 0; i < 30; i++) {
      networkLoop(config);
      scheduler.update();

      bool staConnectedNow = networkIsStaConnected();
      bool apModeNow = networkIsApMode();
      NetworkReadyState netReadyState = NET_READY_NONE;
      if (staConnectedNow) {
        netReadyState = NET_READY_STA;
      } else if (apModeNow) {
        netReadyState = NET_READY_AP;
      }

      if (netReadyState != NET_READY_NONE &&
          netReadyState != lastNetReadyState) {
        ESP_LOGI("main", "System ready!");
        ESP_LOGI("main", "IP Address: %s", getCurrentIpString(config).c_str());
        ESP_LOGI("main", "=================================");
        lastNetReadyState = netReadyState;
      }

      if (staConnectedNow && !lastStaConnectedForNtp) {
        scheduler.updateNTP();
      }
      lastStaConnectedForNtp = staConnectedNow;

      if (scheduler.isTimeValid()) {
        ESP_LOGI("main", "Time synchronized!");
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  } else {
    ESP_LOGI("main", "Skipping time sync (AP mode)");
  }

  transition.forceCurrentBrightness(state.brightness);
  setEffect(state.effect, state.params);
  setBrightness(state.brightness);
  setPower(state.power);

  // --- Main loop ---
  int lastCheckedMinute = -1;
  uint64_t lastFrame = 0;
  std::string lastPreset;
  bool lastPower = false;
  uint8_t lastBrightness = 0;
  std::string lastIp;
  while (true) {
    if (otaInProgress) {
      handleArduinoOTA();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (scheduler.isTimeValid()) {
      checkAndApplyScheduleAfterBoot();
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

    bool staConnectedNow = networkIsStaConnected();
    bool apModeNow = networkIsApMode();
    NetworkReadyState netReadyState = NET_READY_NONE;
    if (staConnectedNow) {
      netReadyState = NET_READY_STA;
    } else if (apModeNow) {
      netReadyState = NET_READY_AP;
    }

    if (netReadyState != NET_READY_NONE && netReadyState != lastNetReadyState) {
      ESP_LOGI("main", "System ready!");
      ESP_LOGI("main", "IP Address: %s", getCurrentIpString(config).c_str());
      ESP_LOGI("main", "=================================");
      lastNetReadyState = netReadyState;
    }

    if (staConnectedNow && !lastStaConnectedForNtp) {
      scheduler.updateNTP();
    }
    lastStaConnectedForNtp = staConnectedNow;

    uint64_t now = esp_timer_get_time() / 1000;
    if (now - lastFrame >= (1000 / FRAMES_PER_SECOND)) {
      lastFrame = now;
      updateLEDs();
      std::string presetName = "-";
      if (state.preset < config.getPresetCount()) {
        presetName = config.presets[state.preset].name;
      }
      std::string ipStr = getCurrentIpString(config);
      if (presetName != lastPreset || state.power != lastPower ||
          state.brightness != lastBrightness || ipStr != lastIp) {
#ifdef DISPLAY_ENABLED
        display_status(presetName.c_str(), state.power, ipStr.c_str());
#endif
        lastPreset = presetName;
        lastPower = state.power;
        lastBrightness = state.brightness;
        lastIp = ipStr;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to yield
  }
}

void setupLEDs() {
  busManager.setupStrip(config.led.type, config.led.colorOrder, config.led.pin,
                        config.led.count);
}

void handleScheduledPreset(int8_t presetId, int currentMinutes) {
  if (manualPowerOffOverride) {
    return;
  }
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
