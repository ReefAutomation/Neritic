#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Version
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "AquariumLED"



// LED Configuration
#define MAX_LED_COUNT 512
#define FRAMES_PER_SECOND 60

// Safety Defaults
#define ABSOLUTE_MIN_TRANSITION 2000      // Hardware minimum 2 seconds
#define ABSOLUTE_MAX_BRIGHTNESS 255

// NTP Configuration
#define NTP_UPDATE_INTERVAL 3600000  // 1 hour

// File Paths
#define CONFIG_FILE "/config.json"
#define PRESET_FILE "/presets.json"

// Limits


// Presets now use WS2812FX native effect index directly (uint8_t)

// Timer Types
enum TimerType {
    TIMER_REGULAR = 0,
    TIMER_SUNRISE = 1,
    TIMER_SUNSET = 2
};

// Configuration Structures
struct LEDConfig {
    uint8_t pin;
    uint16_t count;
    String type;
    String colorOrder;
    int relayPin;
    bool relayActiveHigh; // true: HIGH=on, false: LOW=on
};

struct SafetyConfig {
    uint32_t minTransitionTime;
    uint8_t maxBrightness; // percent (1-100)
};

struct NetworkConfig {
    String hostname;
    String apPassword;
    String ssid;
    String password;
};

struct TimeConfig {
    String ntpServer;
    String timezone; // IANA timezone string, e.g. "America/Los_Angeles"
    float latitude;
    float longitude;
    bool dstEnabled;
};

struct EffectParams {
    uint8_t speed = 100; // percent (0–100)
    uint8_t intensity = 128;
    uint32_t color1 = 0x0000FF;  // Blue
    uint32_t color2 = 0x00FFFF;  // Cyan
};

struct Timer {
    bool enabled = false;
    TimerType type = TIMER_REGULAR;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t presetId = 0;
    uint8_t brightness = 100; // percent (0-100)
    bool operator==(const Timer& other) const {
        return enabled == other.enabled &&
                type == other.type &&
                hour == other.hour &&
                minute == other.minute &&
                presetId == other.presetId &&
                brightness == other.brightness;
    }
};

struct Preset {
    String name = "";
    uint8_t effect = 0; // WS2812FX native effect index
    EffectParams params;
    bool enabled = true;
};


// Global Configuration Class
class Configuration {
public:
    LEDConfig led;
    SafetyConfig safety;
    NetworkConfig network;
    TimeConfig time;
    std::vector<Preset> presets;

    size_t getPresetCount() const { return presets.size(); }
    std::vector<Timer> timers;

    bool load();
    bool save();
    bool factoryReset();
    void setDefaults();

    // GPS and timezone helpers
    void updateLocationFromGPS(float lat, float lon, bool valid);
    int getTimezoneOffsetSeconds(); // Returns offset in seconds for current timezone
    std::vector<String> getSupportedTimezones();

    bool saveToFile(const char* path, const JsonDocument& doc);
    bool loadFromFile(const char* path, JsonDocument& doc); // <-- move to public

    // Partial update from JSON (only update fields present)
    void partialUpdate(const JsonObject& update);

    // Helper to load timers from a JsonArray
    void loadTimersFromJson(JsonArray timersArray);
};

#endif
