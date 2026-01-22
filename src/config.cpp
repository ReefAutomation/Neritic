#include "presets.h"
using std::vector;

#include "inc/config_default.inc"
#include "inc/timezones_json.inc"
#include "config.h"

#include <vector>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "debug.h"

#define FILESYSTEM LittleFS

// Serialize the current configuration to a JSON string for API
String Configuration::toJsonString() {
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
        const auto& t = timers[i];
        JsonObject timerObj = timersArray.createNestedObject();
        timerObj["id"] = i;
        timerObj["enabled"] = t.enabled;
        timerObj["type"] = t.type;
        timerObj["hour"] = t.hour;
        timerObj["minute"] = t.minute;
        timerObj["presetId"] = t.presetId;
        timerObj["brightness"] = hexToPercent(t.brightness);
    }
    String output;
    serializeJson(doc, output);
    return output;
}
// Ensure filesystem is mounted before any file operation
static bool ensureFilesystemMounted() {
    static bool mounted = false;
    if (!mounted) {
        if (!FILESYSTEM.begin()) {
            if (!FILESYSTEM.format()) {
                return false;
            }
            if (!FILESYSTEM.begin()) {
                return false;
            }
        }
        mounted = true;
    }
    return true;
}

// Recursively merge src into dst, filling missing/null fields from src
void mergeJson(JsonVariant dst, JsonVariantConst src) {
    JsonObject dstObj = dst.as<JsonObject>();
    JsonObjectConst srcObj = src.as<JsonObjectConst>();
    if (srcObj.isNull() || dstObj.isNull()) return;
    for (JsonPairConst kv : srcObj) {
        const char* key = kv.key().c_str();
        if (!dstObj.containsKey(key) || dstObj[key].isNull()) {
            dstObj[key] = kv.value();
        } else if (kv.value().is<JsonObjectConst>() && dstObj[key].is<JsonObject>()) {
            mergeJson(dstObj[key], kv.value());
        }
    }
}



// Loads config file and converts percent to hex for internal use
bool Configuration::loadFromFile(const char* path, JsonDocument& doc) {
    if (!ensureFilesystemMounted()) return false;
    File file = FILESYSTEM.open(path, "r");
    if (!file) {
        return false;
    }
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        return false;
    }
    return true;
}


// Saves config file, converting hex to percent for human-readable storage
bool Configuration::saveToFile(const char* path, const JsonDocument& doc) {
    if (!ensureFilesystemMounted()) return false;
    File file = FILESYSTEM.open(path, "w");
    if (!file) {
        return false;
    }
    size_t written = serializeJson(doc, file);
    file.flush();
    file.close();
    delay(10);
    return written > 0;
}


bool Configuration::load() {
    // Load defaults from config_default.inc
    StaticJsonDocument<2048> doc;
    StaticJsonDocument<2048> defaultsDoc;
    DeserializationError errDefault = deserializeJson(defaultsDoc, web_config_default, web_config_default_len);
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

    // (copy the field assignment logic from before, but now doc is always complete)
    // LED Configuration
    if (doc.containsKey("led")) {
        JsonObject ledObj = doc["led"];
        led.pin = ledObj["pin"];
        led.count = ledObj["count"];
        led.type = ledObj["type"].as<String>();
        led.colorOrder = ledObj["colorOrder"].as<String>();
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
        if (netObj.containsKey("hostname")) network.hostname = netObj["hostname"].as<String>();
        if (netObj.containsKey("apPassword")) network.apPassword = netObj["apPassword"].as<String>();
        if (netObj.containsKey("ssid")) network.ssid = netObj["ssid"].as<String>();
        if (netObj.containsKey("password")) network.password = netObj["password"].as<String>();
    }
    // Time Configuration
    if (doc.containsKey("time")) {
        JsonObject timeObj = doc["time"];
        time.ntpServer = timeObj["ntpServer"].as<String>();
        time.timezone = timeObj["timezone"].as<String>();
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
void Configuration::partialUpdate(const JsonObject& update) {
    if (update.containsKey("led")) {
        JsonObject ledObj = update["led"];
        if (ledObj.containsKey("pin")) led.pin = ledObj["pin"];
        if (ledObj.containsKey("count")) led.count = ledObj["count"];
        if (ledObj.containsKey("type")) led.type = ledObj["type"].as<String>();
        if (ledObj.containsKey("colorOrder")) led.colorOrder = ledObj["colorOrder"].as<String>();
        if (ledObj.containsKey("relayPin")) led.relayPin = ledObj["relayPin"];
        if (ledObj.containsKey("relayActiveHigh")) led.relayActiveHigh = ledObj["relayActiveHigh"];
    }
    if (update.containsKey("safety")) {
        JsonObject safetyObj = update["safety"];
        if (safetyObj.containsKey("minTransitionTime")) safety.minTransitionTime = safetyObj["minTransitionTime"];
        if (safetyObj.containsKey("maxBrightness")) {
            int percent = safetyObj["maxBrightness"];
            safety.maxBrightness = percentToHex(percent);
        }
    }
    if (update.containsKey("transitionTimes")) {
        JsonObject tObj = update["transitionTimes"];
        if (tObj.containsKey("powerOn")) transitionTimes.powerOn = tObj["powerOn"];
        if (tObj.containsKey("schedule")) transitionTimes.schedule = tObj["schedule"];
        if (tObj.containsKey("manual")) transitionTimes.manual = tObj["manual"];
        if (tObj.containsKey("effect")) transitionTimes.effect = tObj["effect"];
    }
    if (update.containsKey("network")) {
        JsonObject netObj = update["network"];
        if (netObj.containsKey("hostname")) network.hostname = netObj["hostname"].as<String>();
        if (netObj.containsKey("apPassword")) network.apPassword = netObj["apPassword"].as<String>();
        if (netObj.containsKey("ssid")) network.ssid = netObj["ssid"].as<String>();
        // Only update password if present and non-empty
        if (netObj.containsKey("password")) {
            String newPass = netObj["password"].as<String>();
            if (!newPass.isEmpty()) network.password = newPass;
        }
    }
    if (update.containsKey("time")) {
        JsonObject timeObj = update["time"];
        if (timeObj.containsKey("ntpServer")) time.ntpServer = timeObj["ntpServer"].as<String>();
        if (timeObj.containsKey("timezone")) time.timezone = timeObj["timezone"].as<String>();
        if (timeObj.containsKey("latitude")) time.latitude = timeObj["latitude"].as<double>();
        if (timeObj.containsKey("longitude")) time.longitude = timeObj["longitude"].as<double>();
        if (timeObj.containsKey("dstEnabled")) time.dstEnabled = timeObj["dstEnabled"];
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
    bool ok = true;
    if (FILESYSTEM.exists(CONFIG_FILE)) {
        ok = FILESYSTEM.remove(CONFIG_FILE);
    }
    setDefaults();
    save();
    return ok;
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
    DeserializationError errDefault = deserializeJson(defaultsDoc, web_config_default, web_config_default_len);
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
    DeserializationError err = deserializeJson(tzDoc, web_timezones_json, web_timezones_json_len);
    if (err) return 0;
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
std::vector<String> Configuration::getSupportedTimezones() {
    std::vector<String> timezones;
    StaticJsonDocument<4096> tzDoc;
    DeserializationError err = deserializeJson(tzDoc, web_timezones_json, web_timezones_json_len);
    if (err) return timezones;
    for (JsonObject tz : tzDoc.as<JsonArray>()) {
        if (tz.containsKey("name")) {
            timezones.push_back(tz["name"].as<String>());
        }
    }
    return timezones;
}
