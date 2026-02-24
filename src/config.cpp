#include "presets.h"
using std::vector;

#include "config.h"
#include "inc/config_default.inc"
#include "inc/timezones_json.inc"

#include "debug.h"
#if defined(ESP_IDF_VERSION_MAJOR)
#include <ArduinoJson.h>
#endif
#include "esp_littlefs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "config";

// LittleFS VFS base path
#define FS_BASE "/data"

static bool s_fs_mounted = false;

static bool ensureFilesystemMounted() {
  if (esp_littlefs_mounted("spiffs")) {
    s_fs_mounted = true;
    return true;
  }
  if (s_fs_mounted) return true;
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

// Serialize the current configuration to a JSON string for API
std::string Configuration::toJsonString() {
  #if defined(ESP_IDF_VERSION_MAJOR)
    // ESP-IDF: use ArduinoJson
    StaticJsonDocument<4096> doc;
    JsonObject ledObj = doc.createNestedObject("led");
    ledObj["pin"] = led.pin;
    ledObj["count"] = led.count;
    ledObj["type"] = led.type;
    ledObj["colorOrder"] = led.colorOrder;
    ledObj["relayPin"] = led.relayPin;
    ledObj["relayActiveHigh"] = led.relayActiveHigh;

    JsonObject safetyObj = doc.createNestedObject("safety");
    safetyObj["minTransitionTime"] = safety.minTransitionTime;
    safetyObj["maxBrightness"] = hexToPercent(safety.maxBrightness);

    JsonObject timeObj = doc.createNestedObject("time");
    timeObj["ntpServer"] = time.ntpServer;
    timeObj["timezone"] = time.timezone;
    timeObj["latitude"] = time.latitude;
    timeObj["longitude"] = time.longitude;
    timeObj["dstEnabled"] = time.dstEnabled;

    JsonObject netObj = doc.createNestedObject("network");
    netObj["hostname"] = network.hostname;
    netObj["apPassword"] = network.apPassword;
    netObj["ssid"] = network.ssid;

    JsonObject tObj = doc.createNestedObject("transitionTimes");
    tObj["powerOn"] = transitionTimes.powerOn;
    tObj["schedule"] = transitionTimes.schedule;
    tObj["manual"] = transitionTimes.manual;
    tObj["effect"] = transitionTimes.effect;

    JsonArray timersArray = doc.createNestedArray("timers");
    for (size_t i = 0; i < timers.size(); i++) {
      const auto &t = timers[i];
      JsonObject timerObj = timersArray.createNestedObject();
      timerObj["id"] = i;
      timerObj["enabled"] = t.enabled;
      timerObj["type"] = t.type;
      timerObj["hour"] = t.hour;
      timerObj["minute"] = t.minute;
      timerObj["presetId"] = t.presetId;
      timerObj["brightness"] = hexToPercent(t.brightness);
    }
    std::string output;
    serializeJson(doc, output);
    return output;
  #else
    // Arduino: manual JSON formatting
    std::string json = "{";
    json += "\"led\":{"
      "\"pin\":" + std::to_string(led.pin) + ","
      "\"count\":" + std::to_string(led.count) + ","
      "\"type\":\"" + led.type + "\",";
    json += "\"colorOrder\":\"" + led.colorOrder + "\",";
    json += "\"relayPin\":" + std::to_string(led.relayPin) + ",";
    json += "\"relayActiveHigh\":" + std::to_string(led.relayActiveHigh ? 1 : 0) + "},";

    json += "\"safety\":{"
      "\"minTransitionTime\":" + std::to_string(safety.minTransitionTime) + ","
      "\"maxBrightness\":" + std::to_string(hexToPercent(safety.maxBrightness)) + "},";

    json += "\"time\":{"
      "\"ntpServer\":\"" + time.ntpServer + "\",";
    json += "\"timezone\":\"" + time.timezone + "\",";
    json += "\"latitude\":" + std::to_string(time.latitude) + ",";
    json += "\"longitude\":" + std::to_string(time.longitude) + ",";
    json += "\"dstEnabled\":" + std::to_string(time.dstEnabled ? 1 : 0) + "},";

    json += "\"network\":{"
      "\"hostname\":\"" + network.hostname + "\",";
    json += "\"apPassword\":\"" + network.apPassword + "\",";
    json += "\"ssid\":\"" + network.ssid + "\"},";

    json += "\"transitionTimes\":{"
      "\"powerOn\":" + std::to_string(transitionTimes.powerOn) + ","
      "\"schedule\":" + std::to_string(transitionTimes.schedule) + ","
      "\"manual\":" + std::to_string(transitionTimes.manual) + ","
      "\"effect\":" + std::to_string(transitionTimes.effect) + "},";

    json += "\"timers\":[";
    for (size_t i = 0; i < timers.size(); i++) {
      const auto &t = timers[i];
      if (i > 0) json += ",";
      json += "{"
        "\"id\":" + std::to_string(i) + ","
        "\"enabled\":" + std::to_string(t.enabled ? 1 : 0) + ","
        "\"type\":\"" + t.type + "\",";
      json += "\"hour\":" + std::to_string(t.hour) + ",";
      json += "\"minute\":" + std::to_string(t.minute) + ",";
      json += "\"presetId\":" + std::to_string(t.presetId) + ",";
      json += "\"brightness\":" + std::to_string(hexToPercent(t.brightness)) + "}";
    }
    json += "]}";
    return json;
  #endif
}

// Recursively merge src into dst, filling missing/null fields from src
void mergeJson(JsonVariant dst, JsonVariantConst src) {
  JsonObject dstObj = dst.as<JsonObject>();
  JsonObjectConst srcObj = src.as<JsonObjectConst>();
  if (srcObj.isNull() || dstObj.isNull())
    return;
  for (JsonPairConst kv : srcObj) {
    const char *key = kv.key().c_str();
    if (!dstObj.containsKey(key) || dstObj[key].isNull()) {
      dstObj[key] = kv.value();
    } else if (kv.value().is<JsonObjectConst>() &&
               dstObj[key].is<JsonObject>()) {
      mergeJson(dstObj[key], kv.value());
    }
  }
}

// Loads config file and converts percent to hex for internal use
bool Configuration::loadFromFile(const char *path, JsonDocument &doc) {
  if (!ensureFilesystemMounted()) return false;
  std::string fp = fsPath(path);
  FILE *f = fopen(fp.c_str(), "r");
  if (!f) return false;
  // Read entire file into buffer
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return false; }
  std::vector<char> buf(sz + 1);
  fread(buf.data(), 1, sz, f);
  fclose(f);
  buf[sz] = '\0';
  DeserializationError error = deserializeJson(doc, buf.data());
  return !error;
}

// Saves config file, converting hex to percent for human-readable storage
bool Configuration::saveToFile(const char *path, const JsonDocument &doc) {
  if (!ensureFilesystemMounted()) return false;
  std::string fp = fsPath(path);
  FILE *f = fopen(fp.c_str(), "w");
  if (!f) return false;
  std::string out;
  size_t written = serializeJson(doc, out);
  fwrite(out.c_str(), 1, out.length(), f);
  fflush(f);
  fclose(f);
  vTaskDelay(pdMS_TO_TICKS(10));
  return written > 0;
}

bool Configuration::load() {
  // Load defaults from config_default.inc
  StaticJsonDocument<2048> doc;
  StaticJsonDocument<2048> defaultsDoc;
  DeserializationError errDefault =
      deserializeJson(defaultsDoc, web_config_default, web_config_default_len);
  if (errDefault) {
    setDefaults();
    return false;
  }

  bool updated = false;
  bool loadedFromFile = loadFromFile(CONFIG_FILE, doc);
  if (!loadedFromFile) {
    doc = defaultsDoc;
    updated = true;
  } else {
    // Deep merge: fill missing/null fields from defaults
    mergeJson(doc, defaultsDoc);
    saveToFile(CONFIG_FILE, doc);
    updated = true;
  }

  // (copy the field assignment logic from before, but now doc is always
  // complete) LED Configuration
  if (doc.containsKey("led")) {
    JsonObject ledObj = doc["led"];
    led.pin = ledObj["pin"];
    led.count = ledObj["count"];
    led.type = ledObj["type"] | "WS2812B";
    led.colorOrder = ledObj["colorOrder"] | "GRB";
    led.relayPin = ledObj["relayPin"];
    led.relayActiveHigh = ledObj["relayActiveHigh"];
  }
  // Safety Configuration
  if (doc.containsKey("safety")) {
    JsonObject safetyObj = doc["safety"];
    safety.minTransitionTime = safetyObj["minTransitionTime"];
    int percent = safetyObj["maxBrightness"];
    safety.maxBrightness = percentToHex(percent);
  }
  // Transition Times
  if (doc.containsKey("transitionTimes")) {
    JsonObject tObj = doc["transitionTimes"];
    transitionTimes.powerOn = tObj["powerOn"];
    transitionTimes.schedule = tObj["schedule"];
    transitionTimes.manual = tObj["manual"];
    transitionTimes.effect = tObj["effect"];
  }
  // Network Configuration
  if (doc.containsKey("network")) {
    JsonObject netObj = doc["network"];
    if (netObj.containsKey("hostname"))
      network.hostname = netObj["hostname"] | "deepglow";
    if (netObj.containsKey("apPassword"))
      network.apPassword = netObj["apPassword"] | "";
    if (netObj.containsKey("ssid"))
      network.ssid = netObj["ssid"] | "";
    if (netObj.containsKey("password"))
      network.password = netObj["password"] | "";
  }
  // Time Configuration
  if (doc.containsKey("time")) {
    JsonObject timeObj = doc["time"];
    time.ntpServer = timeObj["ntpServer"] | "pool.ntp.org";
    time.timezone = timeObj["timezone"] | "UTC";
    time.latitude = timeObj["latitude"].as<double>();
    time.longitude = timeObj["longitude"].as<double>();
    time.dstEnabled = timeObj["dstEnabled"];
  }
  // Timers
  if (doc.containsKey("timers")) {
    JsonArray timersArray = doc["timers"];
    loadTimersFromJson(timersArray);
  }

  if (updated) {
    saveToFile(CONFIG_FILE, doc);
  }
  return true;
}

bool Configuration::save() {
  StaticJsonDocument<2048> doc;

  // LED Configuration
  JsonObject ledObj = doc.createNestedObject("led");
  ledObj["pin"] = led.pin;
  ledObj["count"] = led.count;
  ledObj["type"] = led.type;
  ledObj["colorOrder"] = led.colorOrder;
  ledObj["relayPin"] = led.relayPin;
  ledObj["relayActiveHigh"] = led.relayActiveHigh;

  // Safety Configuration
  JsonObject safetyObj = doc.createNestedObject("safety");
  safetyObj["minTransitionTime"] = safety.minTransitionTime;
  safetyObj["maxBrightness"] = hexToPercent(safety.maxBrightness);

  // Transition Times
  JsonObject tObj = doc.createNestedObject("transitionTimes");
  tObj["powerOn"] = transitionTimes.powerOn;
  tObj["schedule"] = transitionTimes.schedule;
  tObj["manual"] = transitionTimes.manual;
  tObj["effect"] = transitionTimes.effect;

  // Network Configuration
  JsonObject netObj = doc.createNestedObject("network");
  netObj["hostname"] = network.hostname;
  netObj["apPassword"] = network.apPassword;
  netObj["ssid"] = network.ssid;
  // DO NOT return password in API response (omit in API, but save to file)
  netObj["password"] = network.password;

  // Time Configuration
  JsonObject timeObj = doc.createNestedObject("time");
  timeObj["ntpServer"] = time.ntpServer;
  timeObj["timezone"] = time.timezone;
  timeObj["latitude"] = time.latitude;
  timeObj["longitude"] = time.longitude;
  timeObj["dstEnabled"] = time.dstEnabled;

  // Timers
  JsonArray timersArray = doc.createNestedArray("timers");
  for (size_t i = 0; i < timers.size(); i++) {
    JsonObject timerObj = timersArray.createNestedObject();
    timerObj["enabled"] = timers[i].enabled;
    timerObj["type"] = timers[i].type;
    timerObj["hour"] = timers[i].hour;
    timerObj["minute"] = timers[i].minute;
    timerObj["presetId"] = timers[i].presetId;
    timerObj["brightness"] = hexToPercent(timers[i].brightness);
  }

  return saveToFile(CONFIG_FILE, doc);
}

// Update only fields present in the received JSON (partial update)
// Example usage: config.partialUpdate(docFromFrontend);
void Configuration::partialUpdate(const JsonObject &update) {
  if (update.containsKey("led")) {
    JsonObject ledObj = update["led"];
    if (ledObj.containsKey("pin"))
      led.pin = ledObj["pin"];
    if (ledObj.containsKey("count"))
      led.count = ledObj["count"];
    if (ledObj.containsKey("type"))
      led.type = (const char*)ledObj["type"];
    if (ledObj.containsKey("colorOrder"))
      led.colorOrder = (const char*)ledObj["colorOrder"];
    if (ledObj.containsKey("relayPin"))
      led.relayPin = ledObj["relayPin"];
    if (ledObj.containsKey("relayActiveHigh"))
      led.relayActiveHigh = ledObj["relayActiveHigh"];
  }
  if (update.containsKey("safety")) {
    JsonObject safetyObj = update["safety"];
    if (safetyObj.containsKey("minTransitionTime"))
      safety.minTransitionTime = safetyObj["minTransitionTime"];
    if (safetyObj.containsKey("maxBrightness")) {
      int percent = safetyObj["maxBrightness"];
      safety.maxBrightness = percentToHex(percent);
    }
  }
  if (update.containsKey("transitionTimes")) {
    JsonObject tObj = update["transitionTimes"];
    if (tObj.containsKey("powerOn"))
      transitionTimes.powerOn = tObj["powerOn"];
    if (tObj.containsKey("schedule"))
      transitionTimes.schedule = tObj["schedule"];
    if (tObj.containsKey("manual"))
      transitionTimes.manual = tObj["manual"];
    if (tObj.containsKey("effect"))
      transitionTimes.effect = tObj["effect"];
  }
  if (update.containsKey("network")) {
    JsonObject netObj = update["network"];
    if (netObj.containsKey("hostname"))
      network.hostname = (const char*)netObj["hostname"];
    if (netObj.containsKey("apPassword"))
      network.apPassword = (const char*)netObj["apPassword"];
    if (netObj.containsKey("ssid"))
      network.ssid = (const char*)netObj["ssid"];
    // Only update password if present and non-empty
    if (netObj.containsKey("password")) {
      const char *newPass = netObj["password"];
      if (newPass && strlen(newPass) > 0)
        network.password = newPass;
    }
  }
  if (update.containsKey("time")) {
    JsonObject timeObj = update["time"];
    if (timeObj.containsKey("ntpServer"))
      time.ntpServer = (const char*)timeObj["ntpServer"];
    if (timeObj.containsKey("timezone"))
      time.timezone = (const char*)timeObj["timezone"];
    if (timeObj.containsKey("latitude"))
      time.latitude = timeObj["latitude"].as<double>();
    if (timeObj.containsKey("longitude"))
      time.longitude = timeObj["longitude"].as<double>();
    if (timeObj.containsKey("dstEnabled"))
      time.dstEnabled = timeObj["dstEnabled"];
  }
  if (update.containsKey("timers")) {
    JsonArray timersArray = update["timers"];
    timers.clear();
    for (size_t i = 0; i < timersArray.size(); i++) {
      JsonObject timerObj = timersArray[i];
      Timer t;
      t.enabled = timerObj["enabled"];
      t.type = (TimerType)timerObj["type"];
      t.hour = timerObj["hour"];
      t.minute = timerObj["minute"];
      t.presetId = timerObj["presetId"];
      // Convert percent to hex at config boundary
      uint8_t percent = timerObj["brightness"] | 100;
      t.brightness = percentToHex(percent);
      timers.push_back(t);
    }
  }
}

// Factory reset: delete config file and restore defaults
bool Configuration::factoryReset() {
  if (!ensureFilesystemMounted()) return false;
  std::string fp = fsPath(CONFIG_FILE);
  remove(fp.c_str()); // ignore error if not exists
  setDefaults();
  save();
  return true;
}

// Helper to load timers from a JsonArray
void Configuration::loadTimersFromJson(JsonArray timersArray) {
  timers.clear();
  for (size_t i = 0; i < timersArray.size(); i++) {
    JsonObject timerObj = timersArray[i];
    Timer t;
    t.enabled = timerObj["enabled"];
    t.type = (TimerType)timerObj["type"];
    t.hour = timerObj["hour"];
    t.minute = timerObj["minute"];
    t.presetId = timerObj["presetId"];
    t.brightness = percentToHex(timerObj["brightness"] | 100);
    timers.push_back(t);
  }
}

void Configuration::setDefaults() {
  led = LEDConfig();
  safety = SafetyConfig();
  network = NetworkConfig();
  time = TimeConfig();

  // Initialize timers from web_config_default
  StaticJsonDocument<2048> defaultsDoc;
  DeserializationError errDefault =
      deserializeJson(defaultsDoc, web_config_default, web_config_default_len);
  if (!errDefault && defaultsDoc.containsKey("timers")) {
    JsonArray timersArray = defaultsDoc["timers"];
    loadTimersFromJson(timersArray);
  }
  savePresets(presets);
}

// Update location from GPS data
void Configuration::updateLocationFromGPS(float lat, float lon, bool valid) {
  time.latitude = lat;
  time.longitude = lon;
}

// Get timezone offset in seconds (stub, needs library for real implementation)
int Configuration::getTimezoneOffsetSeconds() {
  // Use embedded timezone JSON asset for lookup, and add DST if enabled
  StaticJsonDocument<4096> tzDoc;
  DeserializationError err =
      deserializeJson(tzDoc, web_timezones_json, web_timezones_json_len);
  if (err)
    return 0;
  for (JsonObject tz : tzDoc.as<JsonArray>()) {
    if (tz["name"] == time.timezone) {
      double offset = tz["offset"];
      int offsetSeconds = (int)(offset * 3600);
      // Add 1 hour if DST is enabled
      if (time.dstEnabled) {
        offsetSeconds += 3600;
      }
      return offsetSeconds;
    }
  }
  return 0;
}

// Return a vector of all timezone names from the embedded asset
std::vector<std::string> Configuration::getSupportedTimezones() {
  std::vector<std::string> timezones;
  DynamicJsonDocument tzDoc(4096);
  DeserializationError err =
      deserializeJson(tzDoc, web_timezones_json, web_timezones_json_len);
  if (err)
    return timezones;
  for (JsonObject tz : tzDoc.as<JsonArray>()) {
    if (tz.containsKey("name")) {
      const char *n = tz["name"];
      if (n) timezones.push_back(n);
    }
  }
  return timezones;
}
