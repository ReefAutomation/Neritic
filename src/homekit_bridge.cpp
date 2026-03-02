#include "homekit_bridge.h"
#include "esp_log.h"
#include "native_homekit.h"
#include <algorithm>
#include <cJSON.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

#if defined(ENABLE_NATIVE_HOMEKIT)
extern "C" {
#include <hap.h>
}
#endif

static const char *TAG = "homekit";
static const uint8_t HOMEKIT_CATEGORY_LIGHTBULB = 5;

static bool s_initialized = false;
static bool s_lastPower = false;
static uint8_t s_lastBrightness = 0;

#if defined(ENABLE_NATIVE_HOMEKIT) && defined(CONFIG_HAP_MFI_ENABLE)
static constexpr bool NATIVE_APPLE_HOME_CAPABLE = true;
static constexpr const char *NATIVE_AUTH_BACKEND = "mfi";
#elif defined(ENABLE_NATIVE_HOMEKIT)
static constexpr bool NATIVE_APPLE_HOME_CAPABLE = false;
static constexpr const char *NATIVE_AUTH_BACKEND = "dummy";
#else
static constexpr bool NATIVE_APPLE_HOME_CAPABLE = false;
static constexpr const char *NATIVE_AUTH_BACKEND = "unavailable";
#endif

static std::string toBase36Upper(uint64_t value) {
  if (value == 0) {
    return "0";
  }

  std::string out;
  while (value > 0) {
    const uint64_t remainder = value % 36;
    if (remainder < 10) {
      out.push_back((char)('0' + remainder));
    } else {
      out.push_back((char)('A' + (remainder - 10)));
    }
    value /= 36;
  }

  std::reverse(out.begin(), out.end());
  return out;
}

static std::string urlEncode(const std::string &input) {
  static const char *hex = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(input.size() * 3);

  for (unsigned char ch : input) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded.push_back((char)ch);
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[(ch >> 4) & 0x0F]);
      encoded.push_back(hex[ch & 0x0F]);
    }
  }

  return encoded;
}

static std::string sanitizeSetupCode(const std::string &setupCode) {
  std::string digits;
  digits.reserve(setupCode.size());

  for (char ch : setupCode) {
    if (std::isdigit((unsigned char)ch)) {
      digits.push_back(ch);
    }
  }

  return digits;
}

static std::string sanitizeSetupId(const std::string &setupId) {
  std::string out;
  out.reserve(4);

  for (char ch : setupId) {
    if (std::isalnum((unsigned char)ch)) {
      out.push_back((char)std::toupper((unsigned char)ch));
      if (out.size() == 4) {
        break;
      }
    }
  }

  while (out.size() < 4) {
    out.push_back('0');
  }

  return out;
}

static std::string generateHomeKitSetupUri(const Configuration &config) {
#if defined(ENABLE_NATIVE_HOMEKIT)
  std::string normalizedSetupCode = config.homekit.setupCode;
  std::string normalizedSetupId = sanitizeSetupId(config.homekit.setupId);

  if (normalizedSetupCode.size() != 10) {
    return "";
  }

  char *payload = esp_hap_get_setup_payload(
      (char *)normalizedSetupCode.c_str(), (char *)normalizedSetupId.c_str(),
      false, (hap_cid_t)HOMEKIT_CATEGORY_LIGHTBULB);
  if (!payload) {
    return "";
  }

  std::string out(payload);
  std::free(payload);
  return out;
#else
  const std::string codeDigits = sanitizeSetupCode(config.homekit.setupCode);
  if (codeDigits.size() != 8) {
    return "";
  }

  uint64_t setupCodeValue = 0;
  for (char ch : codeDigits) {
    setupCodeValue = (setupCodeValue * 10ULL) + (uint64_t)(ch - '0');
  }

  if (setupCodeValue > 99999999ULL) {
    return "";
  }

  uint32_t valueLow = (uint32_t)setupCodeValue;
  valueLow |= (1u << 28); // supports IP transport

  const uint8_t category = HOMEKIT_CATEGORY_LIGHTBULB;
  if (category & 1u) {
    valueLow |= (1u << 31);
  }

  const uint32_t valueHigh = (uint32_t)(category >> 1);
  const uint64_t payload = (uint64_t)valueLow + ((uint64_t)valueHigh << 32);

  std::string encodedPayload = toBase36Upper(payload);
  while (encodedPayload.size() < 9) {
    encodedPayload = "0" + encodedPayload;
  }

  const std::string normalizedSetupId = sanitizeSetupId(config.homekit.setupId);
  return "X-HM://" + encodedPayload + normalizedSetupId;
#endif
}

std::string homekitBridgeGetSetupUri(const Configuration &config) {
  return generateHomeKitSetupUri(config);
}

std::string homekitBridgeGetQrCodeUrl(const Configuration &config) {
  const std::string setupUri = generateHomeKitSetupUri(config);
  if (setupUri.empty()) {
    return "";
  }
  return "https://quickchart.io/qr?size=360&text=" + urlEncode(setupUri);
}

bool homekitBridgeEnabled(const Configuration &config) {
  return config.homekit.enabled;
}

void homekitBridgeSetup(const Configuration &config) {
  if (!homekitBridgeEnabled(config)) {
    ESP_LOGI(TAG, "HomeKit bridge mode disabled");
    s_initialized = false;
    return;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "HomeKit bridge mode enabled (%s), accessory=%s",
           config.homekit.bridgeMode.c_str(),
           config.homekit.accessoryName.c_str());
  ESP_LOGI(TAG,
           "Use /api/homekit metadata with your Homebridge/Home Assistant bridge");
}

void homekitBridgeLoop() {
  if (!s_initialized) {
    return;
  }
}

void homekitBridgeSync(const SystemState &state) {
  if (!s_initialized) {
    return;
  }
  if (state.power != s_lastPower || state.brightness != s_lastBrightness) {
    s_lastPower = state.power;
    s_lastBrightness = state.brightness;
    ESP_LOGD(TAG, "HomeKit sync state: power=%d brightness=%u",
             state.power ? 1 : 0, state.brightness);
  }
}

std::string homekitBridgeGetStatusJson(const Configuration &config,
                                       const SystemState &state,
                                       const std::string &ipAddress) {
  cJSON *doc = cJSON_CreateObject();
  if (!doc) {
    return "{}";
  }

  const bool enabled = homekitBridgeEnabled(config);
  const bool nativeModeEnabled = nativeHomeKitModeEnabled(config);
  const bool nativeScaffoldCompiled = nativeHomeKitCompiled();
  const bool nativePairingSupported = enabled && nativeScaffoldCompiled &&
                                      nativeModeEnabled &&
                                      NATIVE_APPLE_HOME_CAPABLE;
  cJSON_AddBoolToObject(doc, "enabled", enabled);
  cJSON_AddBoolToObject(doc, "nativePairingSupported", nativePairingSupported);
  cJSON_AddBoolToObject(doc, "nativeScaffoldCompiled", nativeScaffoldCompiled);
  cJSON_AddBoolToObject(doc, "nativeAppleHomeCapable",
                        NATIVE_APPLE_HOME_CAPABLE);
  cJSON_AddStringToObject(doc, "nativeAuthBackend", NATIVE_AUTH_BACKEND);
  cJSON_AddStringToObject(doc, "nativeScaffoldStatus",
                          nativeHomeKitStatusString(config).c_str());
  if (enabled && nativeModeEnabled && nativeScaffoldCompiled &&
      !NATIVE_APPLE_HOME_CAPABLE) {
    cJSON_AddStringToObject(
        doc, "nativePairingUnsupportedReason",
        "Build uses non-MFi HomeKit auth backend; Apple Home direct pairing is not supported.");
  }

#if defined(ENABLE_NATIVE_HOMEKIT)
  int pairedControllers = hap_get_paired_controller_count();
  if (pairedControllers < 0) {
    pairedControllers = 0;
  }
  cJSON_AddNumberToObject(doc, "pairedControllerCount", pairedControllers);
  cJSON_AddBoolToObject(doc, "accessoryPaired", pairedControllers > 0);
#else
  cJSON_AddNumberToObject(doc, "pairedControllerCount", 0);
  cJSON_AddBoolToObject(doc, "accessoryPaired", false);
#endif

  cJSON_AddStringToObject(doc, "bridgeMode", config.homekit.bridgeMode.c_str());
  cJSON_AddStringToObject(doc, "accessoryName",
                          config.homekit.accessoryName.c_str());
  cJSON_AddStringToObject(doc, "setupCode", config.homekit.setupCode.c_str());
  cJSON_AddStringToObject(doc, "setupId", config.homekit.setupId.c_str());
  const std::string setupUri = homekitBridgeGetSetupUri(config);
  if (nativePairingSupported && !setupUri.empty()) {
    cJSON_AddStringToObject(doc, "setupUri", setupUri.c_str());
    const std::string qrCodeUrl = homekitBridgeGetQrCodeUrl(config);
    cJSON_AddStringToObject(doc, "qrCodeUrl", qrCodeUrl.c_str());
    cJSON_AddStringToObject(doc, "qrCodeFormat", "png");
  }
  cJSON_AddBoolToObject(doc, "power", state.power);
  cJSON_AddNumberToObject(doc, "brightness", hexToPercent(state.brightness));
  cJSON_AddStringToObject(doc, "ip", ipAddress.c_str());

  cJSON *endpoints = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "endpoints", endpoints);
  cJSON_AddStringToObject(endpoints, "stateGet", "/api/state");
  cJSON_AddStringToObject(endpoints, "stateSet", "/api/state");
  cJSON_AddStringToObject(endpoints, "metadata", "/api/homekit");

  if (enabled) {
    std::string hint;
    if (nativePairingSupported) {
      hint = "Native HomeKit mode enabled. Ensure the device and iPhone are on the same network and scan the setup QR.";
    } else if (nativeModeEnabled && nativeScaffoldCompiled &&
               !NATIVE_APPLE_HOME_CAPABLE) {
      hint = "Native mode is running with a non-MFi auth backend, so Apple Home rejects direct pairing. Use Homebridge/Home Assistant bridge mode for this build.";
    } else {
      hint = "Direct Apple Home pairing is not supported in bridge mode. Use Homebridge/Home Assistant bridge and map /api/state payloads {\"power\":true|false,\"brightness\":0..100}.";
    }
    cJSON_AddStringToObject(doc, "hint", hint.c_str());
  }

  char *printed = cJSON_PrintUnformatted(doc);
  std::string out = printed ? printed : "{}";
  if (printed) {
    cJSON_free(printed);
  }
  cJSON_Delete(doc);
  return out;
}
