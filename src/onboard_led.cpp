#include "onboard_led.h"
#include "config.h"
#include "esp_log.h"
#include "bus_manager.h"
#include <driver/gpio.h>

#ifdef ONBOARD_RGB_LED
#include "neo_rmt.h"
extern BusManager busManager;

// WiFi status LED on GPIO 8 (WS2812 RGB, GRB order, single pixel)
static const char *TAG = "onboard_led";
static NeoRmtStrip *s_onboardRgbLed = nullptr;
static bool s_onboardRgbLedInitialized = false;

// ── Initialize WiFi status LED (hardcoded - single pixel on GPIO 8)
void initOnboardRgbLed() {
  if (s_onboardRgbLedInitialized) {
    return;
  }

  ESP_LOGI(TAG, "Initializing WiFi status LED on GPIO %d", ONBOARD_RGB_LED);

  // Create NeoRmtStrip for single pixel (WS2812 RGB, GRB order)
  s_onboardRgbLed = new NeoRmtStrip(1, ONBOARD_RGB_LED, false, true); // rgbw=false, grbOrder=true
  if (!s_onboardRgbLed) {
    ESP_LOGE(TAG, "Failed to allocate WiFi LED strip");
    return;
  }

  if (!s_onboardRgbLed->Begin()) {
    ESP_LOGE(TAG, "WiFi LED strip Begin() failed");
    delete s_onboardRgbLed;
    s_onboardRgbLed = nullptr;
    return;
  }

  s_onboardRgbLed->StartUpdateTask();
  s_onboardRgbLed->SignalFrameReady();
  
  s_onboardRgbLedInitialized = true;
  ESP_LOGI(TAG, "WiFi status LED initialized (RED on boot, GREEN when connected)");
  
  // Set RED immediately for boot state
  setOnboardRgbLedRed();
}

// ── Set WiFi status LED color (GRB order)
void setOnboardRgbLedColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!s_onboardRgbLed) {
    return;
  }

  uint8_t rgb[3] = {g, r, b};
  s_onboardRgbLed->SetPixelBytes(0, rgb);
  s_onboardRgbLed->SignalFrameReady();
}

// ── Set WiFi status LED to RED (boot state)
void setOnboardRgbLedRed() {
  setOnboardRgbLedColor(10, 0, 0); // R=10, G=0, B=0 (dim red for visibility)
  ESP_LOGI(TAG, "WiFi status LED RED (boot)");
}

// ── Set WiFi status LED to GREEN (WiFi connected)
void setOnboardRgbLedGreen() {
  setOnboardRgbLedColor(0, 10, 0); // R=0, G=10, B=0 (dim green for visibility)
  ESP_LOGI(TAG, "WiFi status LED GREEN (connected)");
}
#endif

// ── Turn on onboard status LED
#ifdef ONBOARD_STATUS_LED
void turnOnStatusLed() {
  uint8_t ledPin = ONBOARD_STATUS_LED;
  
  // Configure for OUTPUT mode with hardware level HIGH (LED ON)
  gpio_set_direction((gpio_num_t)ledPin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)ledPin, 1);
  ESP_LOGI("main", "Onboard Status LED ENABLED - HARDWARE ON");
}

// ── Turn off onboard status LED
void turnOffStatusLed() {
  uint8_t ledPin = ONBOARD_STATUS_LED;
  
  // Configure for OUTPUT mode with hardware level LOW (LED OFF)
  gpio_set_direction((gpio_num_t)ledPin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)ledPin, 0);
  ESP_LOGI("main", "Onboard Status LED DISABLED - HARDWARE OFF");
}
#endif
