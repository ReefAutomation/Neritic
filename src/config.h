#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Version
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "AquariumLED"


// LED Configuration
#define DEFAULT_LED_PIN 2
#define DEFAULT_LED_COUNT 60
#define DEFAULT_LED_TYPE "SK6812"
#define DEFAULT_COLOR_ORDER "GRB"
#define DEFAULT_LED_RELAY_PIN 12
#define MAX_LED_COUNT 512
#define FRAMES_PER_SECOND 60

// Safety Defaults
#define DEFAULT_MIN_TRANSITION_TIME 5000  // 5 seconds minimum
#define DEFAULT_MAX_BRIGHTNESS 200        // Max 200/255 for fish safety
#define ABSOLUTE_MIN_TRANSITION 2000      // Hardware minimum 2 seconds
#define ABSOLUTE_MAX_BRIGHTNESS 255

// Network
#define DEFAULT_HOSTNAME "AquariumLED"
#define DEFAULT_AP_PASSWORD "aquarium123"

// NTP Configuration
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE_OFFSET 0
#define NTP_UPDATE_INTERVAL 3600000  // 1 hour

// File Paths
#define CONFIG_FILE "/config.json"
#define PRESET_FILE "/presets.json"

// Limits
#define MAX_PRESETS 16
#define MAX_TIMERS 8
#define MAX_SUN_TIMERS 2
#define STATE_SAVE_INTERVAL 60000  // Save every 60 seconds


// Presets now use WS2812FX native effect index directly (uint8_t)

// Timer Types
enum TimerType {
    TIMER_REGULAR = 0,
    TIMER_SUNRISE = 1,
    TIMER_SUNSET = 2
};

// Configuration Structures
struct LEDConfig {
    uint8_t pin = DEFAULT_LED_PIN;
    uint16_t count = DEFAULT_LED_COUNT;
    String type = DEFAULT_LED_TYPE;
    String colorOrder = DEFAULT_COLOR_ORDER;
    int relayPin = DEFAULT_LED_RELAY_PIN;
    bool relayActiveHigh = true; // true: HIGH=on, false: LOW=on
};

struct SafetyConfig {
    uint32_t minTransitionTime = DEFAULT_MIN_TRANSITION_TIME;
    uint8_t maxBrightness = DEFAULT_MAX_BRIGHTNESS;
};

struct NetworkConfig {
    String hostname = DEFAULT_HOSTNAME;
    String apPassword = DEFAULT_AP_PASSWORD;
    String ssid = "";
    String password = "";
};

struct TimeConfig {
    String ntpServer = DEFAULT_NTP_SERVER;
    int timezoneOffset = DEFAULT_TIMEZONE_OFFSET;
    float latitude = 0.0;
    float longitude = 0.0;
    bool dstEnabled = false;
};

struct EffectParams {
    uint8_t speed = 128;
    uint8_t intensity = 128;
    uint32_t color1 = 0x0000FF;  // Blue
    uint32_t color2 = 0x00FFFF;  // Cyan
};

struct Timer {
    bool enabled = false;
    TimerType type = TIMER_REGULAR;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t days = 0b1111111;  // All days
    int16_t offset = 0;        // For sunrise/sunset offset in minutes
    uint8_t presetId = 0;
};

struct Preset {
    String name = "";
    uint8_t brightness = 128;
    uint8_t effect = 0; // WS2812FX native effect index
    EffectParams params;
    bool enabled = true;
};

struct SystemState {
    bool power = true;
    uint8_t brightness = 128;
    uint8_t effect = 0;
    EffectParams params;
    uint32_t transitionTime = DEFAULT_MIN_TRANSITION_TIME;
    uint8_t currentPreset = 0;
    bool inTransition = false;
};

// Global Configuration Class
class Configuration {
public:
    LEDConfig led;
    SafetyConfig safety;
    NetworkConfig network;
    TimeConfig time;
    SystemState state;
    Preset presets[MAX_PRESETS];
    Timer timers[MAX_TIMERS + MAX_SUN_TIMERS];

    bool load();
    void resetPresetsFile();
    bool save();
    bool loadPresets();
    bool savePresets();
    void setDefaults();
    void setDefaultPresets();

private:
    bool loadFromFile(const char* path, JsonDocument& doc);
    bool saveToFile(const char* path, const JsonDocument& doc);
};

#endif
