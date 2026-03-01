#include "presets.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inc/presets_json.inc"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>

#define PRESET_MOUNT_POINT "/data"
#define PRESET_FILE "/presets.json"

// Utility to ensure filesystem is mounted
static bool ensureFilesystemMounted() {
  if (esp_littlefs_mounted("spiffs"))
    return true;
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = PRESET_MOUNT_POINT;
  conf.partition_label = "spiffs";
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;
  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE("presets", "LittleFS mount failed: %s", esp_err_to_name(ret));
    return false;
  }
  return true;
}

void resetPresetsFile() {
  if (!ensureFilesystemMounted())
    return;
  char path[64];
  snprintf(path, sizeof(path), "%s%s", PRESET_MOUNT_POINT, PRESET_FILE);
  if (access(path, F_OK) == 0) {
    remove(path);
  }
}

bool loadPresets(std::vector<Preset> &presets) {
  // Delete presets file before loading (force regeneration)
  resetPresetsFile();

  if (!ensureFilesystemMounted()) {
    ESP_LOGW("presets",
             "Filesystem not available, loading from embedded asset");
  }
  size_t capacity = 8192;
  DynamicJsonDocument doc(capacity);

  bool loaded = false;
  char path[64];
  snprintf(path, sizeof(path), "%s%s", PRESET_MOUNT_POINT, PRESET_FILE);
  FILE *file = ensureFilesystemMounted() ? fopen(path, "r") : nullptr;
  if (file) {
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::string content(fsize, '\0');
    fread(&content[0], 1, fsize, file);
    fclose(file);
    DeserializationError err = deserializeJson(doc, content.c_str(), fsize);
    if (!err && doc.containsKey("presets")) {
      loaded = true;
    }
  } else {
    // Load from embedded asset if file missing or invalid
    DeserializationError err =
        deserializeJson(doc, web_presets_json, web_presets_json_len);
    if (!err && doc.containsKey("presets")) {
      loaded = true;
    }
  }
  if (!loaded)
    return false;
  JsonArray presetsArray = doc["presets"];
  presets.clear();
  for (size_t i = 0; i < presetsArray.size(); i++) {
    JsonObject presetObj = presetsArray[i];
    Preset p;
    p.id = presetObj["id"] | i;
    p.name = presetObj["name"] | "";
    p.effect = presetObj["effect"] | 0;
    p.enabled = presetObj["enabled"] | true;
    if (presetObj.containsKey("params")) {
      JsonObject paramsObj = presetObj["params"];
      // Convert speed from percent to 8-bit for internal use
      p.params.speed = paramsObj["speed"].isNull()
                           ? percentToHex(100)
                           : percentToHex((uint8_t)paramsObj["speed"]);
      p.params.intensity = paramsObj["intensity"].isNull()
                               ? percentToHex(50)
                               : percentToHex((uint8_t)paramsObj["intensity"]);
      p.params.colors.clear();
      if (paramsObj.containsKey("colors")) {
        JsonArray colorsArr = paramsObj["colors"].as<JsonArray>();
        for (JsonVariant v : colorsArr) {
          if (v.is<const char *>()) {
            p.params.colors.push_back(std::string(v.as<const char *>()));
          }
        }
      }
    }
    presets.push_back(p);
  }
  return true;
}

bool savePresets(const std::vector<Preset> &presets) {
  // Use DynamicJsonDocument for heap allocation
  size_t capacity = 8192;
  DynamicJsonDocument doc(capacity);
  JsonArray presetsArray = doc.createNestedArray("presets");

  for (size_t i = 0; i < presets.size(); i++) {
    if (presets[i].name.length() == 0 && i > 0)
      continue;
    JsonObject presetObj = presetsArray.createNestedObject();
    presetObj["name"] = presets[i].name;
    presetObj["effect"] = presets[i].effect;
    presetObj["enabled"] = presets[i].enabled;
    JsonObject paramsObj = presetObj.createNestedObject("params");
    // Convert speed from 8-bit internal to percent for storage
    paramsObj["speed"] = hexToPercent(presets[i].params.speed);
    paramsObj["intensity"] = hexToPercent(presets[i].params.intensity);
    JsonArray colorsArr = paramsObj.createNestedArray("colors");
    for (const auto &c : presets[i].params.colors) {
      colorsArr.add(c);
    }
  }

  if (!ensureFilesystemMounted())
    return false;
  char path[64];
  snprintf(path, sizeof(path), "%s%s", PRESET_MOUNT_POINT, PRESET_FILE);
  FILE *fp = fopen(path, "w");
  if (!fp)
    return false;
  std::string out;
  size_t written = serializeJson(doc, out);
  fwrite(out.c_str(), 1, out.length(), fp);
  fflush(fp);
  fclose(fp);
  vTaskDelay(pdMS_TO_TICKS(10));
  return written > 0;
}
