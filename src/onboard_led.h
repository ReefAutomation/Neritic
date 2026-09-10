#ifndef ONBOARD_LED_H
#define ONBOARD_LED_H

#include "neo_rmt.h"
#include "bus_manager.h"
#include <string>

// Initialize onboard WiFi status LED on GPIO 8 (WS2812 RGB, GRB order, single pixel)
// Call this once after network connection is established
void initOnboardRgbLed();

// Turn off WiFi status LED
void setOnboardRgbLedRed();

// Set WiFi status LED to RED
void setOnboardRgbLedGreen();

// Initialize onboard status LED
void turnOnStatusLed();
void turnOffStatusLed();

#endif // ONBOARD_LED_H
