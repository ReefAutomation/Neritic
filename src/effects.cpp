#include "bus_manager.h"
#include "effects.h"
#include "state.h"
#include <array>

extern BusManager busManager;
extern Configuration config;

volatile uint8_t g_effectSpeed = 1;


// Call this after initializing or reconfiguring the strip
void updatePixelCount() {
  busManager.updatePixelCount();
}

// Centralized function to show the strip regardless of type/order
void showStrip() {
  busManager.show();
}

static std::vector<EffectRegistryEntry>& _effectRegistryVec() {
  static std::vector<EffectRegistryEntry> reg;
  return reg;
}

void _registerEffect(const char* name, uint16_t (*handler)()) {
  _effectRegistryVec().push_back({name, handler});
}

const std::vector<EffectRegistryEntry>& getEffectRegistry() {
  return _effectRegistryVec();
}

// Unified pixel color setter for all LED types and color orders
void setPixelColorUnified(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
  uint32_t color = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
  busManager.setPixelColor(i, color);
}

// WLED-inspired color_blend for 24/32-bit colors
static uint32_t color_blend(uint32_t color1, uint32_t color2, uint8_t blend) {
  const uint32_t TWO_CHANNEL_MASK = 0x00FF00FF;
  uint32_t rb1 =  color1       & TWO_CHANNEL_MASK;
  uint32_t wg1 = (color1 >> 8) & TWO_CHANNEL_MASK;
  uint32_t rb2 =  color2       & TWO_CHANNEL_MASK;
  uint32_t wg2 = (color2 >> 8) & TWO_CHANNEL_MASK;
  uint32_t rb3 = ((((rb1 << 8) | rb2) + (rb2 * blend) - (rb1 * blend)) >> 8) &  TWO_CHANNEL_MASK;
  uint32_t wg3 = ((((wg1 << 8) | wg2) + (wg2 * blend) - (wg1 * blend)))      & ~TWO_CHANNEL_MASK;
  return rb3 | wg3;
}

// Solid color effect: fills the strip with color[0]
uint16_t solid_effect() {
  extern std::array<uint32_t, 8> color;
  BusNeoPixel* neo = busManager.getNeoPixelBus();
  if (!neo || !neo->getStrip()) return 0;
  extern SystemState state;
  uint8_t brightness = state.brightness;
  auto scale = [brightness](uint8_t c) -> uint8_t { return (uint16_t(c) * brightness) / 255; };
  // Use only the first color in the array
  uint32_t solidColor = color[0];
  uint8_t r = scale((solidColor >> 16) & 0xFF);
  uint8_t g = scale((solidColor >> 8) & 0xFF);
  uint8_t b = scale(solidColor & 0xFF);
  for (uint16_t i = 0; i < busManager.getPixelCount(); i++) {
    setPixelColorUnified(i, r, g, b);
  }
  showStrip();
  return 0;
}
REGISTER_EFFECT("Solid", solid_effect);

// Blend effect: smoothly blend across all colors (like WLED FX)
uint16_t blend_effect() {
  extern std::array<uint32_t, 8> color;
  extern size_t colorCount;
  BusNeoPixel* neo = busManager.getNeoPixelBus();
  if (!neo || !neo->getStrip()) return 0;
  extern SystemState state;
  uint8_t brightness = state.brightness;
  auto scale = [brightness](uint8_t c) -> uint8_t { return (uint16_t(c) * brightness) / 255; };
  size_t n = colorCount;
  if (n < 2) return 0;

  // WLED-style phase/shift logic for speed
  uint8_t speed = g_effectSpeed;
  // Use a similar mapping as WLED: (millis() * ((speed >> 3) + 1)) >> 8
  uint32_t now = millis();
  uint16_t phase = (now * ((speed >> 3) + 1)) >> 8; // 0..65535, wraps naturally

  for (uint16_t i = 0; i < busManager.getPixelCount(); i++) {
    // Calculate which segment and local blend
    uint8_t blend_phase = (phase + (i * 256 / std::max(1u, static_cast<unsigned int>(busManager.getPixelCount()-1)))) % 256;
    size_t seg = (blend_phase * (n - 1)) / 256;
    size_t seg_next = (seg + 1) % n;
    uint8_t local_blend = (blend_phase * (n - 1)) % 256;
    uint32_t blended = color_blend(color[seg], color[seg_next], local_blend);
    uint8_t r = scale((blended >> 16) & 0xFF);
    uint8_t g = scale((blended >> 8) & 0xFF);
    uint8_t b = scale(blended & 0xFF);
    setPixelColorUnified(i, r, g, b);
  }
  showStrip();
  return 0;
}
REGISTER_EFFECT("Blend", blend_effect);
