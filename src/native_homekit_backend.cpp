#include "native_homekit_backend.h"
#include "hap_engine.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "native_hk_be";
static bool s_initialized = false;
static bool s_lastPower = false;
static uint8_t s_lastBrightnessPercent = 0;
static NativeHomeKitAccessoryPowerCallback s_powerCallback = nullptr;
static NativeHomeKitAccessoryBrightnessCallback s_brightnessCallback = nullptr;

static bool engineApplyPower(bool power) {
  if (!s_powerCallback) {
    return false;
  }
  return s_powerCallback(power);
}

static bool engineApplyBrightness(uint8_t brightnessPercent) {
  if (!s_brightnessCallback) {
    return false;
  }
  return s_brightnessCallback(brightnessPercent);
}

bool nativeHomeKitBackendInit(const char *accessoryName, const char *setupCode,
                              const char *setupId) {
#ifdef ENABLE_NATIVE_HOMEKIT
  HapEngineConfig engineCfg = {
      .accessoryName = accessoryName ? accessoryName : "DeepGlow",
      .setupCode = setupCode ? setupCode : "031-45-154",
      .setupId = setupId ? setupId : "DG01",
  };
  s_initialized = hapEngineInit(engineCfg, engineApplyPower, engineApplyBrightness);
  ESP_LOGI(TAG, "Backend %s for accessory=%s",
           s_initialized ? "initialized" : "failed",
           accessoryName ? accessoryName : "DeepGlow");
  return s_initialized;
#else
  (void)accessoryName;
  (void)setupCode;
  (void)setupId;
  s_initialized = false;
  return false;
#endif
}

void nativeHomeKitBackendLoop() {
  if (!s_initialized) {
    return;
  }
  hapEngineLoop();
}

void nativeHomeKitBackendSetState(bool power, uint8_t brightnessPercent) {
  if (!s_initialized) {
    return;
  }

  if (power != s_lastPower || brightnessPercent != s_lastBrightnessPercent) {
    s_lastPower = power;
    s_lastBrightnessPercent = brightnessPercent;
    hapEngineNotifyState(power, brightnessPercent);
    ESP_LOGD(TAG, "Backend state update: power=%d brightness=%u",
             power ? 1 : 0, brightnessPercent);
  }
}

void nativeHomeKitBackendRegisterAccessoryCallbacks(
    NativeHomeKitAccessoryPowerCallback powerCallback,
    NativeHomeKitAccessoryBrightnessCallback brightnessCallback) {
  s_powerCallback = powerCallback;
  s_brightnessCallback = brightnessCallback;
}

const char *nativeHomeKitBackendStatus() {
  const char *engineStatus = hapEngineStatus();
  if (engineStatus && engineStatus[0] != '\0' &&
      std::strcmp(engineStatus, "not-compiled") != 0) {
    return engineStatus;
  }

  if (!s_initialized) {
    return "engine-disabled";
  }
  if (!s_powerCallback || !s_brightnessCallback) {
    return "engine-missing-callbacks";
  }
  return engineStatus;
}
