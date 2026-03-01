#include "presets.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inc/presets_json.inc"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

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
  bool loaded = false;
  std::string content;
  char path[64];
  snprintf(path, sizeof(path), "%s%s", PRESET_MOUNT_POINT, PRESET_FILE);
  FILE *file = ensureFilesystemMounted() ? fopen(path, "r") : nullptr;
  if (file) {
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    content.assign((size_t)fsize, '\0');
    fread(&content[0], 1, (size_t)fsize, file);
    fclose(file);
    loaded = !content.empty();
  } else {
    // Load from embedded asset if file missing or invalid
    content.assign((const char *)web_presets_json,
                   (size_t)web_presets_json_len);
    loaded = !content.empty();
  }
  if (!loaded)
    return false;

  cJSON *root = cJSON_Parse(content.c_str());
  if (!root)
    return false;

  cJSON *presetsArray = cJSON_GetObjectItemCaseSensitive(root, "presets");
  if (!cJSON_IsArray(presetsArray)) {
    cJSON_Delete(root);
    return false;
  }

  presets.clear();
  size_t i = 0;
  cJSON *presetObj = nullptr;
  cJSON_ArrayForEach(presetObj, presetsArray) {
    if (!cJSON_IsObject(presetObj)) {
      i++;
      continue;
    }

    Preset p;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(presetObj, "id");
    p.id = cJSON_IsNumber(id) ? (uint8_t)id->valueint : (uint8_t)i;

    cJSON *name = cJSON_GetObjectItemCaseSensitive(presetObj, "name");
    p.name = (cJSON_IsString(name) && name->valuestring) ? name->valuestring
                                                          : "";

    cJSON *effect = cJSON_GetObjectItemCaseSensitive(presetObj, "effect");
    p.effect = cJSON_IsNumber(effect) ? (uint8_t)effect->valueint : 0;

    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(presetObj, "enabled");
    p.enabled = cJSON_IsBool(enabled) ? cJSON_IsTrue(enabled) : true;

    cJSON *paramsObj = cJSON_GetObjectItemCaseSensitive(presetObj, "params");
    if (cJSON_IsObject(paramsObj)) {
      // Convert speed from percent to 8-bit for internal use
      cJSON *speed = cJSON_GetObjectItemCaseSensitive(paramsObj, "speed");
      p.params.speed = cJSON_IsNumber(speed)
                           ? percentToHex((uint8_t)speed->valueint)
                           : percentToHex(100);

      cJSON *intensity =
          cJSON_GetObjectItemCaseSensitive(paramsObj, "intensity");
      p.params.intensity = cJSON_IsNumber(intensity)
                               ? percentToHex((uint8_t)intensity->valueint)
                               : percentToHex(50);

      p.params.colors.clear();
      cJSON *colors = cJSON_GetObjectItemCaseSensitive(paramsObj, "colors");
      if (cJSON_IsArray(colors)) {
        cJSON *color = nullptr;
        cJSON_ArrayForEach(color, colors) {
          if (cJSON_IsString(color) && color->valuestring) {
            p.params.colors.push_back(std::string(color->valuestring));
          }
        }
      }
    }
    presets.push_back(p);
    i++;
  }

  cJSON_Delete(root);
  return true;
}

bool savePresets(const std::vector<Preset> &presets) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return false;

  cJSON *presetsArray = cJSON_CreateArray();
  if (!presetsArray) {
    cJSON_Delete(root);
    return false;
  }
  cJSON_AddItemToObject(root, "presets", presetsArray);

  for (size_t i = 0; i < presets.size(); i++) {
    if (presets[i].name.length() == 0 && i > 0)
      continue;

    cJSON *presetObj = cJSON_CreateObject();
    if (!presetObj)
      continue;
    cJSON_AddItemToArray(presetsArray, presetObj);

    cJSON_AddStringToObject(presetObj, "name", presets[i].name.c_str());
    cJSON_AddNumberToObject(presetObj, "effect", presets[i].effect);
    cJSON_AddBoolToObject(presetObj, "enabled", presets[i].enabled);

    cJSON *paramsObj = cJSON_CreateObject();
    if (!paramsObj)
      continue;
    cJSON_AddItemToObject(presetObj, "params", paramsObj);

    // Convert speed from 8-bit internal to percent for storage
    cJSON_AddNumberToObject(paramsObj, "speed",
                            hexToPercent(presets[i].params.speed));
    cJSON_AddNumberToObject(paramsObj, "intensity",
                            hexToPercent(presets[i].params.intensity));

    cJSON *colorsArr = cJSON_CreateArray();
    if (!colorsArr)
      continue;
    cJSON_AddItemToObject(paramsObj, "colors", colorsArr);

    for (const auto &c : presets[i].params.colors) {
      cJSON_AddItemToArray(colorsArr, cJSON_CreateString(c.c_str()));
    }
  }

  if (!ensureFilesystemMounted())
  {
    cJSON_Delete(root);
    return false;
  }

  char path[64];
  snprintf(path, sizeof(path), "%s%s", PRESET_MOUNT_POINT, PRESET_FILE);
  FILE *fp = fopen(path, "w");
  if (!fp) {
    cJSON_Delete(root);
    return false;
  }

  char *printed = cJSON_PrintUnformatted(root);
  if (!printed) {
    fclose(fp);
    cJSON_Delete(root);
    return false;
  }

  size_t outLen = strlen(printed);
  size_t written = fwrite(printed, 1, outLen, fp);
  fflush(fp);
  fclose(fp);
  cJSON_free(printed);
  cJSON_Delete(root);
  vTaskDelay(pdMS_TO_TICKS(10));
  return written == outLen;
}
