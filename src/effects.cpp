#include "state.h"
#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>
#include "bus_manager.h"
#include "effects.h"
#include "colors.h"
#include "transition.h"

// === Global externs and variables ===
extern std::array<uint32_t, 8> color;
extern SystemState state;
extern EffectParams transitionPrevParams;
extern PendingTransitionState pendingTransition;
extern BusManager busManager;
extern Configuration config;

volatile uint8_t g_effectSpeed = 1;
static std::vector<uint32_t>* g_effectBuffer = nullptr;
static size_t g_ledCount = 0;

// === Typedefs ===
typedef void (*EffectFrameGen)();

// === Forward declarations ===
void effect_solid_frame();
void effect_blend_frame();
void effect_flow_frame();
void effect_chase_frame();

// === Registry ===
std::vector<EffectRegistryEntry> effectRegistry;

// === Frame generator functions ===
void effect_solid_frame() {
  uint32_t c = color[0];
  uint8_t r, g, b, w;
  unpack_rgbw(c, r, g, b, w);
  scale_rgbw_brightness(r, g, b, w, state.brightness, r, g, b, w);
  if (!g_effectBuffer) return;
  for (size_t i = 0; i < g_ledCount; ++i) {
    (*g_effectBuffer)[i] = pack_rgbw(r, g, b, w);
  }
}
REGISTER_EFFECT(0, "Solid", effect_solid_frame)

void effect_blend_frame() {
  if (!g_effectBuffer) return;
  size_t colorCount = state.params.colors.size();
  if (colorCount < 2) {
    for (size_t i = 0; i < g_ledCount; ++i) (*g_effectBuffer)[i] = 0;
    return;
  }
  std::vector<uint32_t> stops;
  for (size_t i = 0; i < colorCount; ++i) {
    const char* cstr = state.params.colors[i].c_str();
    stops.push_back((uint32_t)strtoul(cstr + (cstr[0] == '#' ? 1 : 0), nullptr, 16));
  }
  uint32_t now = millis();
  uint8_t speed = state.params.speed > 0 ? state.params.speed : 50;
  uint32_t period = 10000 - ((speed - 1) * 9000 / 99);
  float phase = float(now % period) / float(period);
  for (size_t i = 0; i < g_ledCount; ++i) {
    float ledPos = float(i) / float(g_ledCount - 1);
    float shiftedPos = fmodf(ledPos + phase, 1.0f);
    float stopPos = shiftedPos * (colorCount - 1);
    int stopIdx = int(stopPos);
    float frac = stopPos - stopIdx;
    uint32_t c1 = stops[stopIdx];
    uint32_t c2 = (stopIdx < int(colorCount - 1)) ? stops[stopIdx + 1] : stops[colorCount - 1];
    uint8_t r, g, b, w;
    blend_rgbw_brightness(c1, c2, frac, state.brightness, r, g, b, w);
    (*g_effectBuffer)[i] = pack_rgbw(r, g, b, w); // RRGGBBWW
  }
}
REGISTER_EFFECT(1, "Blend", effect_blend_frame)

void effect_flow_frame() {
  if (!g_effectBuffer) return;
  if (g_ledCount == 0) return;
  size_t colorCount = state.params.colors.size();
  std::vector<uint32_t> stops;
  for (size_t i = 0; i < colorCount; ++i) {
    const char* cstr = state.params.colors[i].c_str();
    stops.push_back((uint32_t)strtoul(cstr + (cstr[0] == '#' ? 1 : 0), nullptr, 16));
  }
  // Calculate counter based on speed
  uint32_t now = millis();
  uint8_t speed = state.params.speed > 0 ? state.params.speed : 50;
  uint32_t counter = 0;
  if (speed != 0) {
    counter = now * ((speed >> 2) + 1);
    counter = counter >> 8;
  }

  // Determine number of zones
  size_t maxZones = g_ledCount / 6;
  size_t intensity = state.params.intensity > 0 ? state.params.intensity : 128;
  size_t zones = (intensity * maxZones) >> 8;
  if (zones & 0x01) zones++;
  if (zones < 2) zones = 2;
  size_t zoneLen = g_ledCount / zones;
  size_t offset = (g_ledCount - zones * zoneLen) >> 1;

  // Helper: get color from palette (wraps palette if needed)
  auto get_palette_color = [&](int idx) -> uint32_t {
    if (colorCount == 0) return 0;
    float pos = float((idx % 256 + 256) % 256) / 255.0f;
    float scaled = pos * (colorCount - 1);
    size_t i0 = size_t(scaled);
    size_t i1 = (i0 + 1 < colorCount) ? i0 + 1 : i0;
    float frac = scaled - float(i0);
    uint32_t c0 = stops[i0];
    uint32_t c1 = stops[i1];
    uint8_t r, g, b, w;
    blend_rgbw_brightness(c0, c1, frac, state.brightness, r, g, b, w);
    return pack_rgbw(r, g, b, w);
  };

  // Fill all LEDs with background palette color
  for (size_t i = 0; i < g_ledCount; ++i) {
    (*g_effectBuffer)[i] = get_palette_color(-int(counter));
  }

  // Draw zones
  for (size_t z = 0; z < zones; ++z) {
    size_t pos = offset + z * zoneLen;
    for (size_t i = 0; i < zoneLen; ++i) {
      int colorIndex = int(i * 255 / zoneLen) - int(counter);
      size_t led = (z & 0x01) ? i : (zoneLen - 1) - i;
      if (pos + led < g_ledCount)
        (*g_effectBuffer)[pos + led] = get_palette_color(colorIndex);
    }
  }
}
REGISTER_EFFECT(2, "Flow", effect_flow_frame)

void effect_chase_frame() {
  if (!g_effectBuffer) return;
  if (g_ledCount == 0) return;
  uint32_t now = millis();
  uint8_t speed = state.params.speed > 0 ? state.params.speed : 50;
  uint8_t size = state.params.intensity > 0 ? state.params.intensity : 8; // default chase size
  uint32_t period = 2000 - ((speed - 1) * 1800 / 99); // 2000ms (slow) to 200ms (fast)
  float phase = float(now % period) / float(period);
  size_t chaseLen = (size * g_ledCount) / 255;
  if (chaseLen < 1) chaseLen = 1;
  size_t chaseStart = size_t(phase * (g_ledCount + chaseLen)) % (g_ledCount + chaseLen);
  for (size_t i = 0; i < g_ledCount; ++i) {
    bool inChase = (i >= chaseStart && i < chaseStart + chaseLen);
    uint32_t c = inChase ? color[0] : color[1];
    uint8_t r, g, b, w;
    unpack_rgbw(c, r, g, b, w);
    scale_rgbw_brightness(r, g, b, w, state.brightness, r, g, b, w);
    (*g_effectBuffer)[i] = pack_rgbw(r, g, b, w);
  }
}
REGISTER_EFFECT(3, "Chase", effect_chase_frame)

// === Core rendering function ===
void renderEffectToBuffer(uint8_t effectId, const EffectParams& params, std::vector<uint32_t>& buffer, size_t ledCount, const std::array<uint32_t, 8>& colors, size_t colorCount, uint8_t brightness) {
  // Save current global state
  auto old_state = state;
  std::array<uint32_t, 8> old_color = color;
  uint8_t old_brightness = state.brightness;
  std::vector<uint32_t>* old_g_effectBuffer = g_effectBuffer;
  size_t old_g_ledCount = g_ledCount;

  // Set globals to requested values
  state.params = params;
  state.brightness = brightness;
  for (size_t i = 0; i < 8; ++i) color[i] = (i < colorCount) ? colors[i] : 0;
  g_effectBuffer = &buffer;
  g_ledCount = ledCount;

  if (effectId < effectRegistry.size() && effectRegistry[effectId].fn) {
    effectRegistry[effectId].fn();
  } else {
    // fallback: fill with black
    for (size_t i = 0; i < ledCount; ++i) buffer[i] = 0;
  }

  // Restore previous global state
  state = old_state;
  color = old_color;
  state.brightness = old_brightness;
  g_effectBuffer = old_g_effectBuffer;
  g_ledCount = old_g_ledCount;

}

uint32_t getEffectDelayMs(const EffectParams& params) {
  uint8_t speed = params.speed > 0 ? params.speed : 50; // Default to 50 if not set
  // Map speed (1-100) to delay (fast: 10ms, slow: 200ms)
  return 200 - ((speed - 1) * 190 / 99);
}

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

// === Miscellaneous functions ===
void updatePixelCount() {
  busManager.updatePixelCount();
}

void showStrip() {
  busManager.show();
}
