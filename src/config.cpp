#include "presets.h"
using std::vector;

#include "config.h"

#include <cJSON.h>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <string>
#include <time.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include "assets.h"

static const char *TAG = "config";

#define FS_BASE "/data"
#define CONFIG_BACKUP_FILE "/config.json.bak"

static bool s_fs_mounted = false;

// Timezone list served to the web UI. Process-global (it is derived from the
// timezone data, not from per-instance state) and guarded, because the main
// task rebuilds it while the HTTP task reads it.
static std::vector<Configuration::TimezoneOption> s_timezoneOptions;
static std::mutex s_timezoneOptionsMutex;

static bool ensureFilesystemMounted() {
  if (esp_littlefs_mounted("spiffs")) {
    s_fs_mounted = true;
    return true;
  }
  if (s_fs_mounted)
    return true;
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = FS_BASE;
  conf.partition_label = "spiffs";
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;
  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
    return false;
  }
  s_fs_mounted = true;
  return true;
}

static std::string fsPath(const char *path) {
  return std::string(FS_BASE) + path;
}

static bool fileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

static bool readFileToString(const std::string &path, std::string &content) {
  FILE *f = fopen(path.c_str(), "r");
  if (!f)
    return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0) {
    fclose(f);
    return false;
  }
  content.clear();
  content.reserve((size_t)sz);
  char buf[256];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    content.append(buf, n);
  fclose(f);
  return true;
}

static bool writeStringToFile(const std::string &path,
                              const std::string &content) {
  FILE *f = fopen(path.c_str(), "w");
  if (!f)
    return false;
  size_t written = fwrite(content.data(), 1, content.size(), f);
  fflush(f);
  fclose(f);
  vTaskDelay(pdMS_TO_TICKS(10));
  return written == content.size();
}

static cJSON *tryParseConfigJson(const std::string &content) {
  if (content.empty())
    return nullptr;

  cJSON *parsed = cJSON_Parse(content.c_str());
  if (parsed)
    return parsed;

  size_t firstBrace = content.find('{');
  size_t lastBrace = content.rfind('}');
  if (firstBrace != std::string::npos && lastBrace != std::string::npos &&
      lastBrace > firstBrace) {
    std::string candidate =
        content.substr(firstBrace, lastBrace - firstBrace + 1);
    parsed = cJSON_Parse(candidate.c_str());
    if (parsed) {
      ESP_LOGW(TAG,
               "Recovered config by trimming leading/trailing garbage bytes");
      return parsed;
    }
  }

  size_t ledKey = content.find("\"led\"");
  if (ledKey != std::string::npos && lastBrace != std::string::npos &&
      lastBrace > ledKey) {
    std::string candidate =
        "{" + content.substr(ledKey, lastBrace - ledKey + 1);
    parsed = cJSON_Parse(candidate.c_str());
    if (parsed) {
      ESP_LOGW(TAG,
               "Recovered config by dropping corrupted JSON prefix before \"led\"");
      return parsed;
    }
  }

  return nullptr;
}

static const cJSON *jsonObjectItemConst(const cJSON *obj, const char *key) {
  if (!cJSON_IsObject(obj))
    return nullptr;
  return cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
}

static cJSON *jsonObjectItem(cJSON *obj, const char *key) {
  if (!cJSON_IsObject(obj))
    return nullptr;
  return cJSON_GetObjectItemCaseSensitive(obj, key);
}

static const char *jsonStringOr(const cJSON *obj, const char *key,
                                const char *fallback) {
  const cJSON *item = jsonObjectItemConst(obj, key);
  if (cJSON_IsString(item) && item->valuestring)
    return item->valuestring;
  return fallback;
}

static int jsonIntOr(const cJSON *obj, const char *key, int fallback) {
  const cJSON *item = jsonObjectItemConst(obj, key);
  if (cJSON_IsNumber(item))
    return item->valueint;
  if (cJSON_IsBool(item))
    return cJSON_IsTrue(item) ? 1 : 0;
  return fallback;
}

static double jsonDoubleOr(const cJSON *obj, const char *key, double fallback) {
  const cJSON *item = jsonObjectItemConst(obj, key);
  if (cJSON_IsNumber(item))
    return item->valuedouble;
  return fallback;
}

static bool jsonBoolOr(const cJSON *obj, const char *key, bool fallback) {
  const cJSON *item = jsonObjectItemConst(obj, key);
  if (cJSON_IsBool(item))
    return cJSON_IsTrue(item);
  if (cJSON_IsNumber(item))
    return item->valueint != 0;
  return fallback;
}

// Build a DST-less POSIX TZ string from a standard-time UTC offset in hours.
// POSIX offsets are inverted: "UTC-5:30" means UTC+5:30, "UTC5" means UTC-5.
static std::string posixStdOnly(double offsetHours) {
  int totalMin =
      (int)(offsetHours * 60.0 + (offsetHours >= 0 ? 0.5 : -0.5));
  if (totalMin == 0)
    return "UTC0";
  int h = abs(totalMin) / 60;
  int m = abs(totalMin) % 60;
  char sign = totalMin > 0 ? '-' : '+';
  char buf[32];
  if (m)
    snprintf(buf, sizeof(buf), "UTC%c%d:%02d", sign, h, m);
  else
    snprintf(buf, sizeof(buf), "UTC%c%d", sign, h);
  return std::string(buf);
}

// Load the timezone database as a parsed JSON array. Prefer the runtime
// data fetched to the filesystem; fall back to the embedded bootstrap list.
// Returns null (and logs) on failure.
static cJSON *loadTimezoneDoc() {
  if (ensureFilesystemMounted()) {
    std::string content;
    if (readFileToString(fsPath(TIMEZONES_FILE), content)) {
      cJSON *doc = cJSON_Parse(content.c_str());
      if (cJSON_IsArray(doc))
        return doc;
      if (doc)
        cJSON_Delete(doc);
    }
  }
  std::string embedded((const char *)timezones_json, TIMEZONES_JSON_SIZE);
  cJSON *doc = cJSON_Parse(embedded.c_str());
  if (!cJSON_IsArray(doc)) {
    if (doc)
      cJSON_Delete(doc);
    ESP_LOGE(TAG, "Embedded timezone data is not a JSON array");
    return nullptr;
  }
  return doc;
}

// True if the parsed timezone array contains an entry for `name`. An empty
// name is treated as present so callers can skip the check.
static bool timezoneDocHas(cJSON *doc, const char *name) {
  if (!name || !*name)
    return true;
  cJSON *tz = nullptr;
  cJSON_ArrayForEach(tz, doc) {
    if (cJSON_IsObject(tz) && strcmp(jsonStringOr(tz, "name", ""), name) == 0)
      return true;
  }
  return false;
}

static bool mergeJson(cJSON *dst, const cJSON *src) {
  if (!cJSON_IsObject(dst) || !cJSON_IsObject(src))
    return false;

  bool changed = false;

  for (const cJSON *item = src->child; item; item = item->next) {
    const char *key = item->string;
    if (!key)
      continue;

    cJSON *dstItem = cJSON_GetObjectItemCaseSensitive(dst, key);
    if (!dstItem) {
      cJSON_AddItemToObject(dst, key, cJSON_Duplicate(item, 1));
      changed = true;
      continue;
    }

    if (cJSON_IsNull(dstItem)) {
      cJSON_ReplaceItemInObjectCaseSensitive(dst, key, cJSON_Duplicate(item, 1));
      changed = true;
      continue;
    }

    if (cJSON_IsObject(dstItem) && cJSON_IsObject(item)) {
      if (mergeJson(dstItem, item)) {
        changed = true;
      }
    }
  }

  return changed;
}

static void debugDumpFileContents(const char *path) {
  if (!ensureFilesystemMounted()) {
    ESP_LOGW(TAG, "Config dump skipped: filesystem not mounted");
    return;
  }

  std::string fp = fsPath(path);
  std::string content;
  if (!readFileToString(fp, content)) {
    ESP_LOGW(TAG, "Config dump: file not found at %s", fp.c_str());
    return;
  }

  ESP_LOGI(TAG, "===== BOOT CONFIG DUMP START (%s, %u bytes) =====", path,
           (unsigned)content.size());
  if (content.empty()) {
    ESP_LOGI(TAG, "<empty>");
  } else {
    const size_t chunkSize = 180;
    for (size_t i = 0; i < content.size(); i += chunkSize) {
      size_t len = chunkSize;
      if (i + len > content.size())
        len = content.size() - i;
      std::string chunk = content.substr(i, len);
      ESP_LOGI(TAG, "%s", chunk.c_str());
    }
  }
  ESP_LOGI(TAG, "===== BOOT CONFIG DUMP END (%s) =====", path);
}

std::string Configuration::toJsonString() {
  cJSON *doc = cJSON_CreateObject();
  if (!doc)
    return "{}";

  cJSON *ledObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "led", ledObj);
  cJSON_AddNumberToObject(ledObj, "pin", led.pin);
  cJSON_AddNumberToObject(ledObj, "count", led.count);
  cJSON_AddStringToObject(ledObj, "type", led.type.c_str());
  cJSON_AddStringToObject(ledObj, "colorOrder", led.colorOrder.c_str());
  cJSON_AddNumberToObject(ledObj, "relayPin", led.relayPin);
  cJSON_AddBoolToObject(ledObj, "relayActiveHigh", led.relayActiveHigh);

  cJSON *safetyObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "safety", safetyObj);
  cJSON_AddNumberToObject(safetyObj, "minTransitionTime",
                          safety.minTransitionTime);
  cJSON_AddNumberToObject(safetyObj, "maxBrightness",
                          hexToPercent(safety.maxBrightness));

  cJSON *timeObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "time", timeObj);
  cJSON_AddStringToObject(timeObj, "ntpServer", time.ntpServer.c_str());
  cJSON_AddStringToObject(timeObj, "timezone", time.timezone.c_str());
  cJSON_AddNumberToObject(timeObj, "latitude", time.latitude);
  cJSON_AddNumberToObject(timeObj, "longitude", time.longitude);

  cJSON *netObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "network", netObj);
  cJSON_AddStringToObject(netObj, "hostname", network.hostname.c_str());
  cJSON_AddStringToObject(netObj, "apPassword", network.apPassword.c_str());
  cJSON_AddStringToObject(netObj, "ssid", network.ssid.c_str());

  cJSON *homekitObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "homekit", homekitObj);
  cJSON_AddBoolToObject(homekitObj, "enabled", homekit.enabled);
  cJSON_AddStringToObject(homekitObj, "bridgeMode", homekit.bridgeMode.c_str());
  cJSON_AddStringToObject(homekitObj, "accessoryName",
                          homekit.accessoryName.c_str());
  cJSON_AddStringToObject(homekitObj, "setupCode", homekit.setupCode.c_str());
  cJSON_AddStringToObject(homekitObj, "setupId", homekit.setupId.c_str());

  cJSON *tObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "transitionTimes", tObj);
  cJSON_AddNumberToObject(tObj, "powerOn", transitionTimes.powerOn);
  cJSON_AddNumberToObject(tObj, "schedule", transitionTimes.schedule);
  cJSON_AddNumberToObject(tObj, "manual", transitionTimes.manual);
  cJSON_AddNumberToObject(tObj, "effect", transitionTimes.effect);

  cJSON *timersArray = cJSON_CreateArray();
  cJSON_AddItemToObject(doc, "timers", timersArray);
  for (size_t i = 0; i < timers.size(); i++) {
    const auto &t = timers[i];
    cJSON *timerObj = cJSON_CreateObject();
    cJSON_AddItemToArray(timersArray, timerObj);
    cJSON_AddNumberToObject(timerObj, "id", (int)i);
    cJSON_AddBoolToObject(timerObj, "enabled", t.enabled);
    cJSON_AddNumberToObject(timerObj, "type", (int)t.type);
    cJSON_AddNumberToObject(timerObj, "hour", t.hour);
    cJSON_AddNumberToObject(timerObj, "minute", t.minute);
    cJSON_AddNumberToObject(timerObj, "presetId", t.presetId);
    cJSON_AddNumberToObject(timerObj, "brightness", hexToPercent(t.brightness));
  }

  char *printed = cJSON_PrintUnformatted(doc);
  std::string output = printed ? printed : "{}";
  if (printed)
    cJSON_free(printed);
  cJSON_Delete(doc);
  return output;
}

bool Configuration::loadFromFile(const char *path, cJSON **docOut) {
  if (!docOut)
    return false;
  if (!ensureFilesystemMounted())
    return false;
  std::string fp = fsPath(path);
  std::string content;
  if (!readFileToString(fp, content) || content.empty())
    return false;
  cJSON *doc = tryParseConfigJson(content);
  if (!doc) {
    ESP_LOGW(TAG, "Failed to parse %s", path);
    return false;
  }
  *docOut = doc;
  return true;
}

bool Configuration::saveToFile(const char *path, const cJSON *doc) {
  if (!ensureFilesystemMounted())
    return false;

  char *printed = cJSON_PrintUnformatted((cJSON *)doc);
  if (!printed)
    return false;
  std::string out = printed;
  cJSON_free(printed);

  std::string fp = fsPath(path);

  if (strcmp(path, CONFIG_FILE) == 0) {
    std::string tmpPath = fp + ".tmp";
    std::string bakPath = fsPath(CONFIG_BACKUP_FILE);

    if (!writeStringToFile(tmpPath, out)) {
      ESP_LOGE(TAG, "Failed writing temp config file: %s", tmpPath.c_str());
      return false;
    }

    std::string verifyContent;
    cJSON *verifyDoc = nullptr;
    if (!readFileToString(tmpPath, verifyContent) ||
        !(verifyDoc = tryParseConfigJson(verifyContent))) {
      ESP_LOGE(TAG, "Temp config validation failed, keeping current config");
      remove(tmpPath.c_str());
      return false;
    }
    cJSON_Delete(verifyDoc);

    if (fileExists(fp)) {
      remove(bakPath.c_str());
      if (rename(fp.c_str(), bakPath.c_str()) != 0) {
        ESP_LOGE(TAG, "Failed to rotate config backup (%d)", errno);
        remove(tmpPath.c_str());
        return false;
      }
    }

    if (rename(tmpPath.c_str(), fp.c_str()) != 0) {
      ESP_LOGE(TAG, "Failed to activate new config (%d), restoring backup",
               errno);
      remove(tmpPath.c_str());
      if (fileExists(bakPath))
        rename(bakPath.c_str(), fp.c_str());
      return false;
    }

    return true;
  }

  return writeStringToFile(fp, out);
}

bool Configuration::load() {
  debugDumpFileContents(CONFIG_FILE);
  debugDumpFileContents(CONFIG_BACKUP_FILE);

  std::string defaultsContent((const char *)config_json, CONFIG_JSON_SIZE);
  cJSON *defaultsDoc = cJSON_Parse(defaultsContent.c_str());
  if (!defaultsDoc) {
    setDefaults();
    return false;
  }

  cJSON *doc = nullptr;
  bool updated = false;

  bool loadedFromFile = loadFromFile(CONFIG_FILE, &doc);
  if (!loadedFromFile) {
    ESP_LOGW(TAG, "Primary config invalid, trying backup: %s",
             CONFIG_BACKUP_FILE);
    bool loadedFromBackup = loadFromFile(CONFIG_BACKUP_FILE, &doc);
    if (loadedFromBackup) {
      ESP_LOGW(TAG, "Recovered configuration from backup");
      mergeJson(doc, defaultsDoc);
      updated = true;
    } else {
      doc = cJSON_Duplicate(defaultsDoc, 1);
      updated = true;
    }
  } else {
    updated = mergeJson(doc, defaultsDoc);
  }

  cJSON *ledObj = jsonObjectItem(doc, "led");
  if (cJSON_IsObject(ledObj)) {
    led.pin = (uint8_t)jsonIntOr(ledObj, "pin", led.pin);
    led.count = (uint16_t)jsonIntOr(ledObj, "count", led.count);
    led.type = jsonStringOr(ledObj, "type", "WS2812B");
    led.colorOrder = jsonStringOr(ledObj, "colorOrder", "GRB");
    led.relayPin = jsonIntOr(ledObj, "relayPin", led.relayPin);
    led.relayActiveHigh =
        jsonBoolOr(ledObj, "relayActiveHigh", led.relayActiveHigh);
  }

  cJSON *safetyObj = jsonObjectItem(doc, "safety");
  if (cJSON_IsObject(safetyObj)) {
    safety.minTransitionTime =
        (uint32_t)jsonIntOr(safetyObj, "minTransitionTime",
                            (int)safety.minTransitionTime);
    int percent = jsonIntOr(safetyObj, "maxBrightness",
                            hexToPercent(safety.maxBrightness));
    safety.maxBrightness = percentToHex(percent);
  }

  cJSON *tObj = jsonObjectItem(doc, "transitionTimes");
  if (cJSON_IsObject(tObj)) {
    transitionTimes.powerOn =
        (uint32_t)jsonIntOr(tObj, "powerOn", transitionTimes.powerOn);
    transitionTimes.schedule =
        (uint32_t)jsonIntOr(tObj, "schedule", transitionTimes.schedule);
    transitionTimes.manual =
        (uint32_t)jsonIntOr(tObj, "manual", transitionTimes.manual);
    transitionTimes.effect =
        (uint32_t)jsonIntOr(tObj, "effect", transitionTimes.effect);
  }

  cJSON *netObj = jsonObjectItem(doc, "network");
  if (cJSON_IsObject(netObj)) {
    network.hostname = jsonStringOr(netObj, "hostname", "neritic");
    network.apPassword = jsonStringOr(netObj, "apPassword", "");
    network.ssid = jsonStringOr(netObj, "ssid", "");
    network.password = jsonStringOr(netObj, "password", "");
  }

  cJSON *homekitObj = jsonObjectItem(doc, "homekit");
  if (cJSON_IsObject(homekitObj)) {
    homekit.enabled = jsonBoolOr(homekitObj, "enabled", homekit.enabled);
    homekit.bridgeMode =
        jsonStringOr(homekitObj, "bridgeMode", homekit.bridgeMode.c_str());
    homekit.accessoryName = jsonStringOr(homekitObj, "accessoryName",
                                         homekit.accessoryName.c_str());
    homekit.setupCode =
        jsonStringOr(homekitObj, "setupCode", homekit.setupCode.c_str());
    homekit.setupId =
        jsonStringOr(homekitObj, "setupId", homekit.setupId.c_str());
  }
  ESP_LOGI(TAG, "Config load: ssid='%s' len=%u", network.ssid.c_str(),
           (unsigned)network.ssid.size());

  cJSON *timeObj = jsonObjectItem(doc, "time");
  if (cJSON_IsObject(timeObj)) {
    time.ntpServer = jsonStringOr(timeObj, "ntpServer", "pool.ntp.org");
    time.timezone = jsonStringOr(timeObj, "timezone", "UTC");
    time.latitude = jsonDoubleOr(timeObj, "latitude", 0.0);
    time.longitude = jsonDoubleOr(timeObj, "longitude", 0.0);
  }

  loadTimersFromJson(jsonObjectItem(doc, "timers"));

  if (updated)
    saveToFile(CONFIG_FILE, doc);

  cJSON_Delete(doc);
  cJSON_Delete(defaultsDoc);
  return true;
}

bool Configuration::save() {
  cJSON *doc = cJSON_CreateObject();
  if (!doc)
    return false;

  cJSON *ledObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "led", ledObj);
  cJSON_AddNumberToObject(ledObj, "pin", led.pin);
  cJSON_AddNumberToObject(ledObj, "count", led.count);
  cJSON_AddStringToObject(ledObj, "type", led.type.c_str());
  cJSON_AddStringToObject(ledObj, "colorOrder", led.colorOrder.c_str());
  cJSON_AddNumberToObject(ledObj, "relayPin", led.relayPin);
  cJSON_AddBoolToObject(ledObj, "relayActiveHigh", led.relayActiveHigh);

  cJSON *safetyObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "safety", safetyObj);
  cJSON_AddNumberToObject(safetyObj, "minTransitionTime",
                          safety.minTransitionTime);
  cJSON_AddNumberToObject(safetyObj, "maxBrightness",
                          hexToPercent(safety.maxBrightness));

  cJSON *tObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "transitionTimes", tObj);
  cJSON_AddNumberToObject(tObj, "powerOn", transitionTimes.powerOn);
  cJSON_AddNumberToObject(tObj, "schedule", transitionTimes.schedule);
  cJSON_AddNumberToObject(tObj, "manual", transitionTimes.manual);
  cJSON_AddNumberToObject(tObj, "effect", transitionTimes.effect);

  cJSON *netObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "network", netObj);
  cJSON_AddStringToObject(netObj, "hostname", network.hostname.c_str());
  cJSON_AddStringToObject(netObj, "apPassword", network.apPassword.c_str());
  cJSON_AddStringToObject(netObj, "ssid", network.ssid.c_str());
  cJSON_AddStringToObject(netObj, "password", network.password.c_str());

  cJSON *homekitObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "homekit", homekitObj);
  cJSON_AddBoolToObject(homekitObj, "enabled", homekit.enabled);
  cJSON_AddStringToObject(homekitObj, "bridgeMode", homekit.bridgeMode.c_str());
  cJSON_AddStringToObject(homekitObj, "accessoryName",
                          homekit.accessoryName.c_str());
  cJSON_AddStringToObject(homekitObj, "setupCode", homekit.setupCode.c_str());
  cJSON_AddStringToObject(homekitObj, "setupId", homekit.setupId.c_str());

  cJSON *timeObj = cJSON_CreateObject();
  cJSON_AddItemToObject(doc, "time", timeObj);
  cJSON_AddStringToObject(timeObj, "ntpServer", time.ntpServer.c_str());
  cJSON_AddStringToObject(timeObj, "timezone", time.timezone.c_str());
  cJSON_AddNumberToObject(timeObj, "latitude", time.latitude);
  cJSON_AddNumberToObject(timeObj, "longitude", time.longitude);

  cJSON *timersArray = cJSON_CreateArray();
  cJSON_AddItemToObject(doc, "timers", timersArray);
  for (size_t i = 0; i < timers.size(); i++) {
    cJSON *timerObj = cJSON_CreateObject();
    cJSON_AddItemToArray(timersArray, timerObj);
    cJSON_AddBoolToObject(timerObj, "enabled", timers[i].enabled);
    cJSON_AddNumberToObject(timerObj, "type", (int)timers[i].type);
    cJSON_AddNumberToObject(timerObj, "hour", timers[i].hour);
    cJSON_AddNumberToObject(timerObj, "minute", timers[i].minute);
    cJSON_AddNumberToObject(timerObj, "presetId", timers[i].presetId);
    cJSON_AddNumberToObject(timerObj, "brightness",
                            hexToPercent(timers[i].brightness));
  }

  bool ok = saveToFile(CONFIG_FILE, doc);
  cJSON_Delete(doc);
  return ok;
}

void Configuration::partialUpdate(const cJSON *update) {
  if (!cJSON_IsObject(update))
    return;

  const cJSON *ledObj = jsonObjectItemConst(update, "led");
  if (cJSON_IsObject(ledObj)) {
    if (jsonObjectItemConst(ledObj, "pin"))
      led.pin = (uint8_t)jsonIntOr(ledObj, "pin", led.pin);
    if (jsonObjectItemConst(ledObj, "count"))
      led.count = (uint16_t)jsonIntOr(ledObj, "count", led.count);
    if (jsonObjectItemConst(ledObj, "type"))
      led.type = jsonStringOr(ledObj, "type", led.type.c_str());
    if (jsonObjectItemConst(ledObj, "colorOrder"))
      led.colorOrder =
          jsonStringOr(ledObj, "colorOrder", led.colorOrder.c_str());
    if (jsonObjectItemConst(ledObj, "relayPin"))
      led.relayPin = jsonIntOr(ledObj, "relayPin", led.relayPin);
    if (jsonObjectItemConst(ledObj, "relayActiveHigh"))
      led.relayActiveHigh =
          jsonBoolOr(ledObj, "relayActiveHigh", led.relayActiveHigh);
  }

  const cJSON *safetyObj = jsonObjectItemConst(update, "safety");
  if (cJSON_IsObject(safetyObj)) {
    if (jsonObjectItemConst(safetyObj, "minTransitionTime"))
      safety.minTransitionTime =
          (uint32_t)jsonIntOr(safetyObj, "minTransitionTime",
                              (int)safety.minTransitionTime);
    if (jsonObjectItemConst(safetyObj, "maxBrightness")) {
      int percent = jsonIntOr(safetyObj, "maxBrightness", 100);
      safety.maxBrightness = percentToHex(percent);
    }
  }

  const cJSON *tObj = jsonObjectItemConst(update, "transitionTimes");
  if (cJSON_IsObject(tObj)) {
    if (jsonObjectItemConst(tObj, "powerOn"))
      transitionTimes.powerOn =
          (uint32_t)jsonIntOr(tObj, "powerOn", transitionTimes.powerOn);
    if (jsonObjectItemConst(tObj, "schedule"))
      transitionTimes.schedule =
          (uint32_t)jsonIntOr(tObj, "schedule", transitionTimes.schedule);
    if (jsonObjectItemConst(tObj, "manual"))
      transitionTimes.manual =
          (uint32_t)jsonIntOr(tObj, "manual", transitionTimes.manual);
    if (jsonObjectItemConst(tObj, "effect"))
      transitionTimes.effect =
          (uint32_t)jsonIntOr(tObj, "effect", transitionTimes.effect);
  }

  const cJSON *netObj = jsonObjectItemConst(update, "network");
  if (cJSON_IsObject(netObj)) {
    if (jsonObjectItemConst(netObj, "hostname"))
      network.hostname =
          jsonStringOr(netObj, "hostname", network.hostname.c_str());
    if (jsonObjectItemConst(netObj, "apPassword"))
      network.apPassword =
          jsonStringOr(netObj, "apPassword", network.apPassword.c_str());
    if (jsonObjectItemConst(netObj, "ssid"))
      network.ssid = jsonStringOr(netObj, "ssid", network.ssid.c_str());
    if (jsonObjectItemConst(netObj, "password")) {
      const char *newPass = jsonStringOr(netObj, "password", "");
      if (newPass && strlen(newPass) > 0)
        network.password = newPass;
    }
  }

  const cJSON *homekitObj = jsonObjectItemConst(update, "homekit");
  if (cJSON_IsObject(homekitObj)) {
    if (jsonObjectItemConst(homekitObj, "enabled"))
      homekit.enabled = jsonBoolOr(homekitObj, "enabled", homekit.enabled);
    if (jsonObjectItemConst(homekitObj, "bridgeMode"))
      homekit.bridgeMode =
          jsonStringOr(homekitObj, "bridgeMode", homekit.bridgeMode.c_str());
    if (jsonObjectItemConst(homekitObj, "accessoryName"))
      homekit.accessoryName = jsonStringOr(homekitObj, "accessoryName",
                                           homekit.accessoryName.c_str());
    if (jsonObjectItemConst(homekitObj, "setupCode"))
      homekit.setupCode =
          jsonStringOr(homekitObj, "setupCode", homekit.setupCode.c_str());
    if (jsonObjectItemConst(homekitObj, "setupId"))
      homekit.setupId =
          jsonStringOr(homekitObj, "setupId", homekit.setupId.c_str());
  }

  const cJSON *timeObj = jsonObjectItemConst(update, "time");
  if (cJSON_IsObject(timeObj)) {
    if (jsonObjectItemConst(timeObj, "ntpServer"))
      time.ntpServer =
          jsonStringOr(timeObj, "ntpServer", time.ntpServer.c_str());
    if (jsonObjectItemConst(timeObj, "timezone"))
      time.timezone = jsonStringOr(timeObj, "timezone", time.timezone.c_str());
    if (jsonObjectItemConst(timeObj, "latitude"))
      time.latitude = jsonDoubleOr(timeObj, "latitude", time.latitude);
    if (jsonObjectItemConst(timeObj, "longitude"))
      time.longitude = jsonDoubleOr(timeObj, "longitude", time.longitude);
  }

  const cJSON *timersArray = jsonObjectItemConst(update, "timers");
  if (cJSON_IsArray(timersArray))
    loadTimersFromJson(timersArray);
}

bool Configuration::factoryReset() {
  if (!ensureFilesystemMounted())
    return false;
  std::string fp = fsPath(CONFIG_FILE);
  remove(fp.c_str());
  setDefaults();
  save();
  return true;
}

void Configuration::loadTimersFromJson(const cJSON *timersArray) {
  if (!cJSON_IsArray(timersArray))
    return;

  timers.clear();
  cJSON *timerObj = nullptr;
  cJSON_ArrayForEach(timerObj, timersArray) {
    if (!cJSON_IsObject(timerObj))
      continue;
    Timer t;
    t.enabled = jsonBoolOr(timerObj, "enabled", false);
    t.type = (TimerType)jsonIntOr(timerObj, "type", (int)TIMER_REGULAR);
    t.hour = (uint8_t)jsonIntOr(timerObj, "hour", 0);
    t.minute = (uint8_t)jsonIntOr(timerObj, "minute", 0);
    t.presetId = (uint8_t)jsonIntOr(timerObj, "presetId", 0);
    t.brightness = percentToHex((uint8_t)jsonIntOr(timerObj, "brightness", 100));
    timers.push_back(t);
  }
}

void Configuration::setDefaults() {
  led = LEDConfig();
  safety = SafetyConfig();
  network = NetworkConfig();
  homekit = HomeKitConfig();
  time = TimeConfig();

  std::string defaultsContent((const char *)config_json, CONFIG_JSON_SIZE);
  cJSON *defaultsDoc = cJSON_Parse(defaultsContent.c_str());
  if (defaultsDoc) {
    loadTimersFromJson(jsonObjectItem(defaultsDoc, "timers"));
    cJSON_Delete(defaultsDoc);
  }
  savePresets(presets);
}

void Configuration::updateLocationFromGPS(float lat, float lon, bool valid) {
  time.latitude = lat;
  time.longitude = lon;
}

// POSIX TZ string for one parsed timezone entry. With DST enabled the entry's
// own rules are used; otherwise (or for an entry without POSIX rules, i.e. the
// older schema) it is derived from the standard offset so a zone never silently
// falls back to UTC.
static std::string posixForEntry(const cJSON *tz) {
  const char *posix = jsonStringOr(tz, "tz", "");
  if (posix && *posix)
    return posix;
  return posixStdOnly(jsonDoubleOr(tz, "offset", 0.0));
}

std::string Configuration::getTimezonePosix(const std::string &name) {
  cJSON *tzDoc = loadTimezoneDoc();
  if (!tzDoc)
    return "";

  std::string result;
  cJSON *tz = nullptr;
  cJSON_ArrayForEach(tz, tzDoc) {
    if (!cJSON_IsObject(tz))
      continue;
    const char *n = jsonStringOr(tz, "name", "");
    if (name != n)
      continue;
    result = posixForEntry(tz);
    break;
  }
  cJSON_Delete(tzDoc);
  return result;
}

// Format an offset in seconds east of UTC the way the UI shows it, e.g.
// "GMT -3", "GMT +5:30", "GMT +0".
static std::string formatGmtOffset(int offsetSeconds) {
  int total = offsetSeconds;
  char sign = total < 0 ? '-' : '+';
  if (total < 0)
    total = -total;
  int hours = total / 3600;
  int minutes = (total % 3600) / 60;
  char buf[24];
  if (minutes)
    snprintf(buf, sizeof(buf), "GMT %c%d:%02d", sign, hours, minutes);
  else
    snprintf(buf, sizeof(buf), "GMT %c%d", sign, hours);
  return std::string(buf);
}

// Offset in seconds east of UTC that a POSIX TZ string is in effect at `now`.
// Reads the wall clock under that zone, then re-reads it as if it were UTC;
// the difference is the offset, which keeps any DST delta correct.
static bool posixOffsetNow(const char *posix, time_t now, int *out) {
  setenv("TZ", posix, 1);
  tzset();

  struct tm wall;
  if (localtime_r(&now, &wall) == nullptr)
    return false;
  wall.tm_isdst = 0; // interpreted against UTC0 below, which has no DST

  setenv("TZ", "UTC0", 1);
  tzset();
  time_t wallAsUtc = mktime(&wall);
  if (wallAsUtc == (time_t)-1)
    return false;

  *out = (int)(wallAsUtc - now);
  return true;
}

void Configuration::refreshTimezoneOptions(bool clockValid) {
  cJSON *tzDoc = loadTimezoneDoc();
  if (!tzDoc) {
    std::lock_guard<std::mutex> lock(s_timezoneOptionsMutex);
    s_timezoneOptions.clear();
    return;
  }

  time_t now = ::time(nullptr);
  std::vector<TimezoneOption> options;
  cJSON *tz = nullptr;
  cJSON_ArrayForEach(tz, tzDoc) {
    if (!cJSON_IsObject(tz))
      continue;
    const char *n = jsonStringOr(tz, "name", nullptr);
    if (!n)
      continue;

    TimezoneOption option;
    option.name = n;

    if (clockValid) {
      std::string posix = posixForEntry(tz);
      int offsetSeconds = 0;
      if (!posix.empty() && posixOffsetNow(posix.c_str(), now, &offsetSeconds))
        option.gmt = formatGmtOffset(offsetSeconds);
    }
    options.push_back(option);
  }
  cJSON_Delete(tzDoc);

  if (clockValid) {
    // posixOffsetNow() left TZ on UTC0/the last zone; put the C library back on
    // the configured zone.
    applyTimezone();
    ESP_LOGI(TAG, "Timezone offsets refreshed (%u zones)",
             (unsigned)options.size());
  }

  std::lock_guard<std::mutex> lock(s_timezoneOptionsMutex);
  s_timezoneOptions = options;
}

std::vector<Configuration::TimezoneOption>
Configuration::getSupportedTimezones() const {
  std::lock_guard<std::mutex> lock(s_timezoneOptionsMutex);
  return s_timezoneOptions;
}

void Configuration::applyTimezone() {
  std::string posix = getTimezonePosix(time.timezone);
  if (posix.empty()) {
    ESP_LOGW(TAG, "Unknown timezone '%s'; falling back to UTC",
             time.timezone.c_str());
    posix = "UTC0";
  }
  setenv("TZ", posix.c_str(), 1);
  tzset();
  ESP_LOGI(TAG, "Timezone applied: %s -> %s", time.timezone.c_str(),
           posix.c_str());
}

bool Configuration::saveTimezoneData(const std::string &content) {
  if (!ensureFilesystemMounted())
    return false;

  cJSON *doc = cJSON_Parse(content.c_str());
  if (!cJSON_IsArray(doc)) {
    if (doc)
      cJSON_Delete(doc);
    ESP_LOGW(TAG, "Ignoring invalid runtime timezone data");
    return false;
  }
  // Never replace good data with a set that lacks the configured zone, which
  // would silently push the device to UTC.
  if (!timezoneDocHas(doc, time.timezone.c_str())) {
    ESP_LOGW(TAG,
             "Runtime timezone data missing configured zone '%s'; keeping "
             "existing data",
             time.timezone.c_str());
    cJSON_Delete(doc);
    return false;
  }
  cJSON_Delete(doc);

  bool ok = writeStringToFile(fsPath(TIMEZONES_FILE), content);
  if (!ok)
    ESP_LOGW(TAG, "Failed to persist runtime timezone data");
  return ok;
}
