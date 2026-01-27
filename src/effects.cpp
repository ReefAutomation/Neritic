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
void effect_solid();
void effect_sunrise();
void effect_sunset();
void effect_moonlight();
void effect_lightning();

// === Registry ===
std::vector<EffectRegistryEntry> effectRegistry;

// === Color blend utility ===
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

// === Frame generator functions ===
void effect_solid() {
  uint32_t c = color[0];
  uint8_t r, g, b, w;
  unpack_rgbw(c, r, g, b, w);
  // Intensity modifier: scale brightness by intensity percent
  uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 255;
  uint8_t mod_brightness = (uint16_t(state.brightness) * intensity) / 255;
  scale_rgbw_brightness(r, g, b, w, mod_brightness, r, g, b, w);
  if (!g_effectBuffer) return;
  for (size_t i = 0; i < g_ledCount; ++i) {
    (*g_effectBuffer)[i] = pack_rgbw(r, g, b, w);
  }
}
REGISTER_EFFECT(0, "Solid", effect_solid)

void effect_sunrise() {
  if (!g_effectBuffer) return;
  size_t colorCount = state.params.colors.size();
  if (colorCount < 2) {
    for (size_t i = 0; i < g_ledCount; ++i) (*g_effectBuffer)[i] = 0;
    return;
  }
  // Prepare palette
  std::vector<uint32_t> stops;
  for (size_t i = 0; i < colorCount; ++i) {
    const char* cstr = state.params.colors[i].c_str();
    stops.push_back((uint32_t)strtoul(cstr + (cstr[0] == '#' ? 1 : 0), nullptr, 16));
  }
  // Persistent pixel buffer for blending
  static std::vector<uint32_t> blendBuffer;
  if (blendBuffer.size() != g_ledCount) blendBuffer.assign(g_ledCount, stops[0]);
  // Timing and speed
  uint32_t now = millis();
  uint8_t speed = state.params.speed > 0 ? state.params.speed : 50;
  uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 255;
  // Intensity modifier: scale blendSpeed
  uint8_t blendSpeed = 10 + ((speed - 1) * (128 - 10) / 99);
  blendSpeed = 1 + ((blendSpeed - 1) * intensity) / 255;
  // Phase for palette shift
  uint32_t shift = (now * ((speed >> 3) + 1)) >> 8;
  for (size_t i = 0; i < g_ledCount; ++i) {
    // Wavy offset for each pixel (quadwave8 analog)
    float wave = 128.0f * (1.0f - cosf(2.0f * 3.14159265f * (float(i + 1) * 16) / 256.0f)); // quadwave8 approx
    size_t paletteIdx = (shift + (uint32_t)wave) % (colorCount * 256);
    size_t stopIdx = paletteIdx / 256;
    float frac = (paletteIdx % 256) / 255.0f;
    uint32_t c1 = stops[stopIdx];
    uint32_t c2 = stops[(stopIdx + 1) % colorCount];
    // Blend previous color toward target palette color
    uint32_t target;
    {
      uint8_t r, g, b, w;
      blend_rgbw_brightness(c1, c2, frac, state.brightness, r, g, b, w);
      target = pack_rgbw(r, g, b, w);
    }
    // Blend current pixel toward target using blendSpeed
    uint32_t prev = blendBuffer[i];
    uint8_t br = blendSpeed;
    blendBuffer[i] = color_blend(prev, target, br);
    (*g_effectBuffer)[i] = blendBuffer[i];
  }
}
REGISTER_EFFECT(1, "Sunrise", effect_sunrise)

void effect_sunset() {
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
  uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 255;
  uint32_t counter = 0;
  if (speed != 0) {
    counter = now * ((speed >> 2) + 1);
    counter = counter >> 8;
  }

  // Determine number of zones
  size_t maxZones = g_ledCount / 6;
  size_t zones = 1 + ((intensity * maxZones) / 255);
  if (zones & 0x01) zones++;
  if (zones < 2) zones = 2;
  size_t zoneLen = g_ledCount / zones;
  size_t offset = (g_ledCount - zones * zoneLen) >> 1;

  // Helper: get color from palette (always wraps, last blends into first)
  auto get_palette_color = [&](int idx) -> uint32_t {
    if (colorCount == 0) return 0;
    int wrapped = ((idx % 256) + 256) % 256;
    float pos = float(wrapped) / 255.0f;
    float scaled = pos * colorCount;
    size_t i0 = size_t(scaled) % colorCount;
    size_t i1 = (i0 + 1) % colorCount;
    float frac = scaled - float(size_t(scaled));
    uint32_t c0 = stops[i0];
    uint32_t c1 = stops[i1];
    uint8_t r, g, b, w;
    blend_rgbw_brightness(c0, c1, frac, state.brightness, r, g, b, w);
    return pack_rgbw(r, g, b, w);
  };

  // Use reverse from params
  bool reverse = state.params.reverse;

  // Fill all LEDs with background palette color
  for (size_t i = 0; i < g_ledCount; ++i) {
    (*g_effectBuffer)[i] = get_palette_color(-int(counter));
  }

  // Draw zones
  for (size_t z = 0; z < zones; ++z) {
    size_t pos = offset + z * zoneLen;
    for (size_t i = 0; i < zoneLen; ++i) {
      int colorIndex = int(i * 255 / zoneLen) - int(counter);
      size_t led = ((z & 0x01) ^ reverse) ? i : (zoneLen - 1) - i;
      if (pos + led < g_ledCount)
        (*g_effectBuffer)[pos + led] = get_palette_color(colorIndex);
    }
  }
}
REGISTER_EFFECT(2, "Sunset", effect_sunset)

void effect_moonlight() {
  if (!g_effectBuffer) return;
  if (g_ledCount == 0) return;

  // Underwater moonlight: soft blue base, moving caustic highlight, gentle shimmer
  // Base color: dim blue/cyan
  uint8_t baseR = 10, baseG = 30, baseB = 60, baseW = 0;
  // Highlight color: brighter blue/cyan
  uint8_t highR = 40, highG = 120, highB = 255, highW = 0;

  uint32_t now = millis();
  // Map speed param (1-255) to a practical, visible range
  uint8_t userSpeed = state.params.speed > 0 ? state.params.speed : 30;
  // At speed=1: 1 cycle per 8s; at speed=255: 1 cycle per 1s
  float minPeriod = 8000.0f; // ms for one cycle at slowest
  float maxPeriod = 1000.0f; // ms for one cycle at fastest
  float t = (userSpeed - 1) / 254.0f;
  float period = minPeriod - t * (minPeriod - maxPeriod);
  float speed = 1.0f / period; // cycles per ms
  // Debug: print speed mapping
  static uint8_t lastDebugSpeed = 0;
  if (userSpeed != lastDebugSpeed) {
    printf("[Moonlight Debug] speed param: %d, period: %g ms, speed: %g cycles/ms\n", userSpeed, period, speed);
    lastDebugSpeed = userSpeed;
  }
  float shimmerSpeed = 0.0015f;
  uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 128;
  float waveLen = 0.08f + 0.32f * (intensity / 255.0f); // how wide the caustic highlight is

  for (size_t i = 0; i < g_ledCount; ++i) {
    float pos = (float)i / g_ledCount;
    float phase = fmodf(now * speed, 1.0f); // ensure phase wraps smoothly
    // Use a raised cosine (Hann window) for the caustic highlight
    float dist = fabsf(pos - phase);
    if (dist > 0.5f) dist = 1.0f - dist; // wrap around
    float caustic = 0.0f;
    if (dist < waveLen) {
      float x = dist / waveLen;
      caustic = 0.5f * (1.0f + cosf(3.14159f * x)); // smooth, no spikes
    }
    // Gentle shimmer, even softer
    float shimmer = 0.85f + 0.15f * sinf(now * shimmerSpeed + i * 0.7f);

    // Blend base and highlight
    float r = baseR * shimmer * (1.0f - caustic) + highR * shimmer * caustic;
    float g = baseG * shimmer * (1.0f - caustic) + highG * shimmer * caustic;
    float b = baseB * shimmer * (1.0f - caustic) + highB * shimmer * caustic;
    float w = baseW * shimmer * (1.0f - caustic) + highW * shimmer * caustic;

    scale_rgbw_brightness((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)w, state.brightness, (uint8_t&)r, (uint8_t&)g, (uint8_t&)b, (uint8_t&)w);
    (*g_effectBuffer)[i] = pack_rgbw((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)w);
  }
}
REGISTER_EFFECT(3, "Moonlight", effect_moonlight)

// Lightning effect: emulates a storm seen from underwater
void effect_lightning() {
    // Debug: print speed and delay info
    static uint8_t lastDebugSpeed = 0;
    static uint32_t lastDebugDelay = 0;
    static uint8_t lastSpeed = 0;
  if (!g_effectBuffer) return;
  if (g_ledCount == 0) return;

  static uint32_t lastFlash = 0;
  static bool inBurst = false;
  static uint32_t burstStart = 0;
  static uint32_t burstDuration = 0;
  static uint32_t burstFlashCount = 0;
  static uint32_t burstFlashIdx = 0;
  static uint32_t flashStart = 0;
  static uint32_t flashLen = 0;
  static uint32_t flashTime = 0;
  static uint32_t flashDuration = 0;
  static float flashIntensity = 0.0f;
  static uint32_t rngSeed = 123456789;
  static uint32_t nextDelay = 2000;

  // Simple LCG for pseudo-randomness
  auto randf = [&]() {
    rngSeed = (rngSeed * 1664525UL + 1013904223UL);
    return (rngSeed & 0xFFFFFF) / float(0xFFFFFF);
  };

  uint32_t now = millis();
  // Use preset colors: first is base, last is flash, middle (if present) is highlight
  uint8_t baseR = 0, baseG = 0, baseB = 0, baseW = 0;
  uint8_t flashR = 0, flashG = 0, flashB = 0, flashW = 0;
  const auto& colors = state.params.colors;
  if (colors.size() > 0) {
    uint32_t c = (uint32_t)strtoul(colors[0].c_str() + (colors[0][0] == '#' ? 1 : 0), nullptr, 16);
    unpack_rgbw(c, baseR, baseG, baseB, baseW);
  }
  if (colors.size() > 1) {
    uint32_t c = (uint32_t)strtoul(colors[colors.size()-1].c_str() + (colors[colors.size()-1][0] == '#' ? 1 : 0), nullptr, 16);
    unpack_rgbw(c, flashR, flashG, flashB, flashW);
  }

  // Recalculate delay immediately if speed changes
  uint8_t userSpeed = state.params.speed > 0 ? state.params.speed : 1;
  if (userSpeed != lastSpeed) {
    // Clamp to [1,255]
    if (userSpeed < 1) userSpeed = 1;
    if (userSpeed > 255) userSpeed = 255;
    uint32_t maxDelay = 60000; // 60s
    uint32_t minDelay = 5000;  // 5s
    float t = (userSpeed - 1) / 254.0f;
    uint32_t baseDelay = (uint32_t)(maxDelay - t * (maxDelay - minDelay));
    float jitter = 0.9f + 0.2f * randf();
    nextDelay = (uint32_t)(baseDelay * jitter);
    lastSpeed = userSpeed;
    // Debug output
    printf("[Lightning Debug] speed param: %d, mapped: %d, baseDelay: %lu ms, nextDelay: %lu ms\n", state.params.speed, userSpeed, (unsigned long)baseDelay, (unsigned long)nextDelay);
  }
  // Flash logic
  if (!inBurst && now - lastFlash > nextDelay) {
    // Start a burst (lightning event)
    inBurst = true;
    burstStart = now;
    burstDuration = 180 + (uint32_t)(randf() * 220); // 180-400ms burst
    burstFlashCount = 2 + (uint32_t)(randf() * 4); // 2-5 flashes per burst
    burstFlashIdx = 0;
    flashTime = now;
    flashDuration = 30 + (uint32_t)(randf() * 60); // 30-90ms per flash
    uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 255;
    float minFlash = 0.1f + 0.7f * (intensity / 255.0f); // min intensity 0.1-0.8
    float maxFlash = 0.5f + 0.5f * (intensity / 255.0f); // max intensity 0.5-1.0
    flashIntensity = minFlash + (maxFlash - minFlash) * randf();
    // Pick a random set of LEDs for the flash
    flashLen = std::max(1U, (uint32_t)(1 + randf() * (g_ledCount - 1)));
    flashStart = (uint32_t)(randf() * g_ledCount);
    lastFlash = now;
  }
  if (inBurst) {
    if (now - flashTime > flashDuration) {
      // Next flash in burst
      burstFlashIdx++;
      if (burstFlashIdx < burstFlashCount) {
        flashTime = now;
        flashDuration = 30 + (uint32_t)(randf() * 60); // 30-90ms
        // Use intensity for flash range in burst
        uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 255;
        float minFlash = 0.1f + 0.7f * (intensity / 255.0f);
        float maxFlash = 0.5f + 0.5f * (intensity / 255.0f);
        flashIntensity = minFlash + (maxFlash - minFlash) * randf();
        flashLen = std::max(1U, (uint32_t)(1 + randf() * (g_ledCount - 1)));
        flashStart = (uint32_t)(randf() * g_ledCount);
      } else {
        inBurst = false;
        flashIntensity = 0.0f;
      }
    }
    // Optionally, fade out last flash at end of burst
    if (burstFlashIdx == burstFlashCount - 1 && (now - flashTime > flashDuration / 2)) {
      float t = 1.0f - float(now - flashTime) / float(flashDuration);
      if (t < 0.2f) flashIntensity *= t / 0.2f;
    }
  } else {
    flashIntensity = 0.0f;
  }

  // Gentle shimmer for underwater
  float shimmerSpeed = 0.0015f;
  for (size_t i = 0; i < g_ledCount; ++i) {
    float shimmer = 0.85f + 0.15f * sinf(now * shimmerSpeed + i * 0.7f);
    // Lightning: randomly distributed flash LEDs
    bool inFlashSet = false;
    if (inBurst && flashIntensity > 0.0f) {
      // Each flash, randomly select which LEDs are lit
      // Use a hash of flashStart, flashLen, and i for deterministic randomness per flash
      uint32_t hash = (uint32_t)(flashStart ^ (i * 2654435761UL) ^ (flashLen * 374761393UL));
      inFlashSet = ((hash % g_ledCount) < flashLen);
    }
    float segIntensity = inFlashSet ? flashIntensity : 0.0f;
    float r = baseR * shimmer * (1.0f - segIntensity) + flashR * segIntensity;
    float g = baseG * shimmer * (1.0f - segIntensity) + flashG * segIntensity;
    float b = baseB * shimmer * (1.0f - segIntensity) + flashB * segIntensity;
    float w = baseW * shimmer * (1.0f - segIntensity) + flashW * segIntensity;
    scale_rgbw_brightness((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)w, state.brightness, (uint8_t&)r, (uint8_t&)g, (uint8_t&)b, (uint8_t&)w);
    (*g_effectBuffer)[i] = pack_rgbw((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)w);
  }
}
REGISTER_EFFECT(4, "Lightning", effect_lightning)

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


// === Miscellaneous functions ===
void updatePixelCount() {
  busManager.updatePixelCount();
}

void showStrip() {
  busManager.show();
}
