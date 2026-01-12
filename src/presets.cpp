#include "presets.h"
#include "web_assets/presets_json.inc"
#include <LittleFS.h>
#include <ArduinoJson.h>

#define FILESYSTEM LittleFS
#define PRESET_FILE "/presets.json"

// Utility to ensure filesystem is mounted
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

void resetPresetsFile() {
    if (!ensureFilesystemMounted()) return;
    if (FILESYSTEM.exists(PRESET_FILE)) {
        FILESYSTEM.remove(PRESET_FILE);
    }
}

bool loadPresets(std::vector<Preset>& presets) {
    // Delete presets file before loading (force regeneration)
    resetPresetsFile();

    size_t capacity = 8192;
    DynamicJsonDocument doc(capacity);

    bool loaded = false;
    File file = FILESYSTEM.open(PRESET_FILE, "r");
    if (file && deserializeJson(doc, file) == DeserializationError::Ok && doc.containsKey("presets")) {
        loaded = true;
        file.close();
    } else {
        // Load from embedded asset if file missing or invalid
        DeserializationError err = deserializeJson(doc, web_presets_json, web_presets_json_len);
        if (!err && doc.containsKey("presets")) {
            loaded = true;
        }
    }
    if (!loaded) return false;
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
            p.params.speed = paramsObj["speed"].isNull() ? 100 : (uint8_t)paramsObj["speed"];
            p.params.intensity = paramsObj["intensity"] | 128;
            p.params.colors.clear();
            if (paramsObj.containsKey("colors")) {
                JsonArray colorsArr = paramsObj["colors"].as<JsonArray>();
                for (JsonVariant v : colorsArr) {
                    if (v.is<const char*>()) {
                        p.params.colors.push_back(String(v.as<const char*>()));
                    }
                }
            }
        }
        presets.push_back(p);
    }
    return true;
}

bool savePresets(const std::vector<Preset>& presets) {
    // Use DynamicJsonDocument for heap allocation
    size_t capacity = 8192;
    DynamicJsonDocument doc(capacity);
    JsonArray presetsArray = doc.createNestedArray("presets");

    for (size_t i = 0; i < presets.size(); i++) {
        if (presets[i].name.length() == 0 && i > 0) continue;
        JsonObject presetObj = presetsArray.createNestedObject();
        presetObj["name"] = presets[i].name;
        presetObj["effect"] = presets[i].effect;
        presetObj["enabled"] = presets[i].enabled;
        JsonObject paramsObj = presetObj.createNestedObject("params");
        paramsObj["speed"] = presets[i].params.speed;
        paramsObj["intensity"] = presets[i].params.intensity;
        JsonArray colorsArr = paramsObj.createNestedArray("colors");
        for (const auto& c : presets[i].params.colors) {
            colorsArr.add(c);
        }
    }

    if (!ensureFilesystemMounted()) return false;
    File file = FILESYSTEM.open(PRESET_FILE, "w");
    if (!file) return false;
    size_t written = serializeJson(doc, file);
    file.flush();
    file.close();
    delay(10);
    return written > 0;
}
