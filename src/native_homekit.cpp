#include "native_homekit.h"
#include "native_homekit_backend.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "network.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

extern void setPower(bool power);
extern void setBrightness(uint8_t brightness);

static const char *TAG = "native_hk";
static bool s_started = false;
static bool s_lastPower = false;
static uint8_t s_lastBrightness = 0;
static bool s_applyFromAccessory = false;
static NativeHomeKitBackendStateCallback s_backendStateCallback = nullptr;
static bool s_shouldEnable = false;
static std::string s_accessoryName;
static std::string s_setupCode;
static std::string s_setupId;
static uint64_t s_lastInitAttemptMs = 0;
static uint64_t s_nextRetryAtMs = 0;
static std::string s_initStatus = "pending";
static uint8_t s_initAttempts = 0;

static constexpr uint64_t NATIVE_HK_INITIAL_RETRY_DELAY_MS = 4000;
static constexpr uint64_t NATIVE_HK_MAX_RETRY_DELAY_MS = 60000;

static bool backendApplyPower(bool power);
static bool backendApplyBrightness(uint8_t brightnessPercent);

static bool tryStartNativeBackend() {
  if (!s_shouldEnable || s_started) {
    return s_started;
  }

  if (!networkIsStaConnected()) {
    s_initStatus = "waiting-network";
    return false;
  }

  if (!nativeHomeKitBackendInit(s_accessoryName.c_str(), s_setupCode.c_str(),
                                s_setupId.c_str())) {
    const char *backendStatus = nativeHomeKitBackendStatus();
    s_initStatus = (backendStatus && backendStatus[0] != '\0')
                       ? std::string("init-failed:") + backendStatus
                       : "init-failed";
    const bool hardFailure =
        backendStatus && std::strcmp(backendStatus, "hap-start-failed") == 0;

    if (hardFailure) {
      s_nextRetryAtMs = UINT64_MAX;
      ESP_LOGE(TAG,
               "Native HomeKit backend init failed with hard error '%s'; retries suppressed",
               backendStatus);
      return false;
    }

    ++s_initAttempts;
    const uint64_t exponentialDelay =
      NATIVE_HK_INITIAL_RETRY_DELAY_MS
      << (s_initAttempts > 5 ? 5 : s_initAttempts - 1);
    const uint64_t retryDelayMs =
      std::min<uint64_t>(NATIVE_HK_MAX_RETRY_DELAY_MS, exponentialDelay);
    s_nextRetryAtMs = (esp_timer_get_time() / 1000) + retryDelayMs;
    ESP_LOGE(TAG,
         "Native HomeKit backend init failed (attempt=%u); next retry in %llu ms",
         (unsigned)s_initAttempts, (unsigned long long)retryDelayMs);
    return false;
  }

  nativeHomeKitBackendRegisterAccessoryCallbacks(backendApplyPower,
                                                 backendApplyBrightness);
  s_started = true;
  s_initAttempts = 0;
  s_nextRetryAtMs = 0;
  s_initStatus = "running";
  ESP_LOGI(TAG, "Native HomeKit backend initialized");
  return true;
}

static bool backendApplyPower(bool power) {
  return nativeHomeKitApplyPowerFromAccessory(power);
}

static bool backendApplyBrightness(uint8_t brightnessPercent) {
  return nativeHomeKitApplyBrightnessFromAccessory(brightnessPercent);
}

static std::string toLowerCopy(const std::string &value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return (char)std::tolower(ch);
  });
  return out;
}

bool nativeHomeKitCompiled() {
#ifdef ENABLE_NATIVE_HOMEKIT
  return true;
#else
  return false;
#endif
}

bool nativeHomeKitModeEnabled(const Configuration &config) {
  const std::string mode = toLowerCopy(config.homekit.bridgeMode);
  return mode == "native" || mode == "hybrid";
}

void nativeHomeKitSetup(const Configuration &config) {
  if (!nativeHomeKitCompiled()) {
    s_shouldEnable = false;
    s_started = false;
    s_initStatus = "not-compiled";
    return;
  }

  s_shouldEnable = config.homekit.enabled && nativeHomeKitModeEnabled(config);
  s_accessoryName = config.homekit.accessoryName;
  s_setupCode = config.homekit.setupCode;
  s_setupId = config.homekit.setupId;
  s_lastInitAttemptMs = 0;
  s_nextRetryAtMs = 0;
  s_initAttempts = 0;

  if (!s_shouldEnable) {
    s_started = false;
    s_initStatus = config.homekit.enabled ? "bridge-mode" : "disabled";
    return;
  }

  s_started = false;
  s_lastPower = false;
  s_lastBrightness = 0;
  s_applyFromAccessory = false;
  s_initStatus = "pending";

  ESP_LOGI(TAG, "Native HomeKit enabled: mode=%s accessory=%s",
           config.homekit.bridgeMode.c_str(), config.homekit.accessoryName.c_str());
}

void nativeHomeKitLoop() {
  if (!s_started && s_shouldEnable) {
    const uint64_t nowMs = esp_timer_get_time() / 1000;
    if (s_nextRetryAtMs != 0 && nowMs < s_nextRetryAtMs) {
      return;
    }

    if (nowMs - s_lastInitAttemptMs >= 2000) {
      s_lastInitAttemptMs = nowMs;
      (void)tryStartNativeBackend();
    }
  }

  if (s_started) {
    nativeHomeKitBackendLoop();
  }
}

void nativeHomeKitSync(const SystemState &state) {
  if (!s_started) {
    return;
  }

  if (state.power != s_lastPower || state.brightness != s_lastBrightness) {
    s_lastPower = state.power;
    s_lastBrightness = state.brightness;

    nativeHomeKitBackendSetState(state.power, hexToPercent(state.brightness));

    if (!s_applyFromAccessory && s_backendStateCallback) {
      s_backendStateCallback(state.power, hexToPercent(state.brightness));
    }

    ESP_LOGD(TAG, "Scaffold sync: power=%d brightness=%u",
             state.power ? 1 : 0, state.brightness);
  }
}

std::string nativeHomeKitStatusString(const Configuration &config) {
  if (!nativeHomeKitCompiled()) {
    return "not-compiled";
  }
  if (!config.homekit.enabled) {
    return "disabled";
  }
  if (!nativeHomeKitModeEnabled(config)) {
    return "bridge-mode";
  }
  if (!s_started) {
    return s_initStatus;
  }
  const char *backendStatus = nativeHomeKitBackendStatus();
  if (!backendStatus || backendStatus[0] == '\0') {
    return "adapter-ready";
  }
  return std::string("adapter-ready:") + backendStatus;
}

void nativeHomeKitSetBackendStateCallback(
    NativeHomeKitBackendStateCallback callback) {
  s_backendStateCallback = callback;
}

bool nativeHomeKitApplyPowerFromAccessory(bool power) {
  if (!nativeHomeKitCompiled() || !s_started) {
    return false;
  }

  s_applyFromAccessory = true;
  setPower(power);
  s_applyFromAccessory = false;
  return true;
}

bool nativeHomeKitApplyBrightnessFromAccessory(uint8_t brightnessPercent) {
  if (!nativeHomeKitCompiled() || !s_started) {
    return false;
  }

  s_applyFromAccessory = true;
  setBrightness(percentToHex(brightnessPercent));
  s_applyFromAccessory = false;
  return true;
}
