#include "effects.h"
#include "bus_manager.h"
#include "colors.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "state.h"
#include "transition.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// === Global externs and variables ===
extern std::array<uint32_t, 8> color;
extern SystemState state;
extern EffectParams transitionPrevParams;
extern TransitionEngine::PendingTransitionState pendingTransition;
extern BusManager busManager;
extern Configuration config;

volatile uint8_t g_effectSpeed = 1;
std::vector<uint32_t> *g_effectBuffer = nullptr;
static size_t g_ledCount = 0;
static SemaphoreHandle_t g_renderMutex = nullptr;

// === Typedefs ===
typedef void (*EffectFrameGen)();

// === Forward declarations ===
void effect_solid();
void effect_sunrise();
void effect_sunset();
void effect_moonlight();
void effect_lightning();
void effect_kelp_forest();
void effect_coral_reef();
void effect_bioluminescence();
void effect_tidal_surge();

// === Registry ===
std::vector<EffectRegistryEntry> effectRegistry;

// === Color blend utility ===
static uint32_t color_blend(uint32_t color1, uint32_t color2, uint8_t blend) {
  const uint32_t TWO_CHANNEL_MASK = 0x00FF00FF;
  uint32_t rb1 = color1 & TWO_CHANNEL_MASK;
  uint32_t wg1 = (color1 >> 8) & TWO_CHANNEL_MASK;
  uint32_t rb2 = color2 & TWO_CHANNEL_MASK;
  uint32_t wg2 = (color2 >> 8) & TWO_CHANNEL_MASK;
  uint32_t rb3 = ((((rb1 << 8) | rb2) + (rb2 * blend) - (rb1 * blend)) >> 8) &
                 TWO_CHANNEL_MASK;
  uint32_t wg3 = ((((wg1 << 8) | wg2) + (wg2 * blend) - (wg1 * blend))) &
                 ~TWO_CHANNEL_MASK;
  return rb3 | wg3;
}

// === Frame generator functions ===
void effect_solid() {
  if (!g_effectBuffer)
    return;
  uint32_t c = color[0];
  uint8_t r, g, b, w;
  unpack_rgbw(c, r, g, b, w);
  scale_rgbw_brightness(r, g, b, w, state.brightness, r, g, b, w);
  for (size_t i = 0; i < g_ledCount; ++i) {
    (*g_effectBuffer)[i] = pack_rgbw(r, g, b, w);
  }
}
REGISTER_EFFECT(0, "Solid", effect_solid)

void effect_sunrise() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  size_t colorCount = state.params.colors.size();
  if (colorCount == 0) {
    for (size_t i = 0; i < g_ledCount; ++i)
      (*g_effectBuffer)[i] = 0;
    return;
  }

  // Prepare palette stops
  std::vector<uint32_t> stops;
  for (size_t i = 0; i < colorCount; ++i) {
    const char *cstr = state.params.colors[i].c_str();
    stops.push_back((uint32_t)strtoul(cstr + (cstr[0] == '#' ? 1 : 0),
                                      nullptr, 16));
  }

  // Interpolate between the first and last palette stops.
  auto get_palette_color = [&](float pos) -> uint32_t {
    if (colorCount == 0)
      return 0;
    // pos expected in [0,1]
    if (pos < 0.0f)
      pos = 0.0f;
    if (pos > 1.0f)
      pos = 1.0f;
    float scaled = pos * float(colorCount - 1);
    size_t i0 = size_t(floorf(scaled));
    size_t i1 = std::min(i0 + 1, colorCount - 1);
    float frac = scaled - float(i0);
    uint32_t c0 = stops[i0];
    uint32_t c1 = stops[i1];
    uint8_t r, g, b, w;
    blend_rgbw_brightness(c0, c1, frac, state.brightness, r, g, b, w);
    return pack_rgbw(r, g, b, w);
  };
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  uint8_t speed = state.params.speed > 0 ? state.params.speed : 50;
  uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 255;

  // Map speed to total duration (ms): faster speed = shorter duration
  // speed=255 -> ~10s, speed=1 -> ~6.5min
  uint32_t durationMs = 10000 + (uint32_t)(255 - speed) * 1500;

  // Persist start time across frames and reset when speed or LED count changes
  static uint32_t startTime = 0;
  static uint8_t lastSpeed = 0;
  static size_t lastLedCount = 0;
  if (startTime == 0 || lastSpeed != speed || lastLedCount != g_ledCount) {
    startTime = now;
    lastSpeed = speed;
    lastLedCount = g_ledCount;
  }

  uint32_t elapsed = now - startTime;
  float progress = durationMs > 0 ? float(elapsed) / float(durationMs) : 1.0f;
  if (progress > 1.0f)
    progress = 1.0f;

  // Compute expanding radius from center
  float maxRadius = float(g_ledCount) * 0.5f;
  float currentRadius = progress * maxRadius;

  // Center position (allow fractional center for even counts)
  float center = (float(g_ledCount) - 1.0f) * 0.5f;

  // Edge softness based on intensity (higher intensity => sharper edge)
  float edgeSoft = 0.5f + 4.0f * (1.0f - (float(intensity) / 255.0f));

  for (size_t i = 0; i < g_ledCount; ++i) {
    float dist = fabsf(float(i) - center);
    float pos = dist / maxRadius;
    if (pos < 0.0f)
      pos = 0.0f;
    if (pos > 1.0f)
      pos = 1.0f;
    uint32_t baseColor = get_palette_color(pos);

    // Determine coverage of this LED in [0,1]
    float cover = (currentRadius - dist) / edgeSoft;
    if (cover <= 0.0f) {
      (*g_effectBuffer)[i] = baseColor;
      continue;
    }
    if (cover > 1.0f)
      cover = 1.0f;

    // Advance the sunrise from the center toward both ends of the strip.
    uint32_t target = get_palette_color(1.0f - pos);

    // Blend the base palette into the advancing sunrise color.
    if (cover < 1.0f) {
      // cover in [0,1] -> blend percent
      uint8_t blendPct = uint8_t(cover * 255.0f);
      (*g_effectBuffer)[i] = color_blend(baseColor, target, blendPct);
    } else {
      (*g_effectBuffer)[i] = target;
    }
  }
}
REGISTER_EFFECT(1, "Sunrise", effect_sunrise)

void effect_sunset() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;
  size_t colorCount = state.params.colors.size();
  std::vector<uint32_t> stops;
  for (size_t i = 0; i < colorCount; ++i) {
    const char *cstr = state.params.colors[i].c_str();
    stops.push_back(
        (uint32_t)strtoul(cstr + (cstr[0] == '#' ? 1 : 0), nullptr, 16));
  }
  // Calculate counter based on speed
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
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
  if (zones & 0x01)
    zones++;
  if (zones < 2)
    zones = 2;
  size_t zoneLen = g_ledCount / zones;
  size_t offset = (g_ledCount - zones * zoneLen) >> 1;

  // Helper: get color from palette (always wraps, last blends into first)
  auto get_palette_color = [&](int idx) -> uint32_t {
    if (colorCount == 0)
      return 0;
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
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  // Underwater moonlight: soft blue base, moving caustic highlight, gentle
  // shimmer Base color: dim blue/cyan
  uint8_t baseR = 10, baseG = 30, baseB = 60, baseW = 0;
  // Highlight color: brighter blue/cyan
  uint8_t highR = 40, highG = 120, highB = 255, highW = 0;

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  // Map speed param (1-255) to a practical, visible range
  uint8_t userSpeed = state.params.speed > 0 ? state.params.speed : 30;
  // At speed=1: 1 cycle per 8s; at speed=255: 1 cycle per 1s
  float minPeriod = 8000.0f; // ms for one cycle at slowest
  float maxPeriod = 1000.0f; // ms for one cycle at fastest
  float t = (userSpeed - 1) / 254.0f;
  float period = minPeriod - t * (minPeriod - maxPeriod);
  float speed = 1.0f / period; // cycles per ms
  float shimmerSpeed = 0.0015f;
  uint8_t intensity = state.params.intensity > 0 ? state.params.intensity : 128;
  float waveLen =
      0.08f + 0.32f * (intensity / 255.0f); // how wide the caustic highlight is

  for (size_t i = 0; i < g_ledCount; ++i) {
    float pos = (float)i / g_ledCount;
    float phase = fmodf(now * speed, 1.0f); // ensure phase wraps smoothly
    // Use a raised cosine (Hann window) for the caustic highlight
    float dist = fabsf(pos - phase);
    if (dist > 0.5f)
      dist = 1.0f - dist; // wrap around
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

    scale_rgbw_brightness((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)w,
                          state.brightness, (uint8_t &)r, (uint8_t &)g,
                          (uint8_t &)b, (uint8_t &)w);
    (*g_effectBuffer)[i] =
        pack_rgbw((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)w);
  }
}
REGISTER_EFFECT(3, "Moonlight", effect_moonlight)

// Lightning effect: emulates a storm seen from underwater
void effect_lightning() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  static bool initialized = false;
  static uint32_t lastEvent = 0;
  static uint32_t phaseStarted = 0;
  static uint32_t phaseDuration = 0;
  static uint32_t flashDuration = 0;
  static uint8_t flashesRemaining = 0;
  static bool burstActive = false;
  static bool flashActive = false;
  static uint32_t rngSeed = 123456789;
  static float flashCenter1 = 0.0f;
  static float flashCenter2 = 0.0f;
  static float flashWidth1 = 0.0f;
  static float flashWidth2 = 0.0f;

  auto randf = [&]() {
    rngSeed = (rngSeed * 1664525UL + 1013904223UL);
    return (rngSeed & 0xFFFFFF) / float(0xFFFFFF);
  };

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const auto &colors = state.params.colors;
  const uint32_t backgroundColor =
      colors.empty()
          ? 0
          : (uint32_t)strtoul(colors[0].c_str() + (colors[0][0] == '#'),
                              nullptr, 16);
  const uint32_t waterColor =
      colors.size() > 2
          ? (uint32_t)strtoul(colors[1].c_str() + (colors[1][0] == '#'),
                              nullptr, 16)
          : backgroundColor;
  const uint32_t flashColor =
      colors.size() > 1
          ? (uint32_t)strtoul(colors.back().c_str() +
                                  (colors.back()[0] == '#'),
                              nullptr, 16)
          : pack_rgbw(210, 235, 255, 0);

  const float speed =
      (state.params.speed > 0 ? state.params.speed : 50) / 255.0f;
  const float intensity = state.params.intensity / 255.0f;
  const uint32_t eventDelay = uint32_t(45000.0f - speed * 38000.0f);
  if (!initialized) {
    lastEvent = now - eventDelay + 1200;
    initialized = true;
  }

  if (!burstActive && now - lastEvent >= eventDelay) {
    burstActive = true;
    flashesRemaining = 2 + uint8_t(randf() * 3.0f);
    phaseStarted = now;
    flashActive = true;
    flashDuration = 100 + uint32_t(randf() * 100.0f);
    phaseDuration = flashDuration;
    flashCenter1 = randf();
    flashCenter2 = randf();
    const float spread = 0.5f + 0.5f * intensity;
    flashWidth1 = (0.16f + randf() * 0.2f) * spread;
    flashWidth2 = (0.12f + randf() * 0.18f) * spread;
  } else if (burstActive && now - phaseStarted >= phaseDuration) {
    if (flashActive) {
      flashActive = false;
      --flashesRemaining;
      if (flashesRemaining == 0) {
        burstActive = false;
        lastEvent = now;
      } else {
        phaseStarted = now;
        phaseDuration = 90 + uint32_t(randf() * 130.0f);
      }
    } else {
      flashActive = true;
      phaseStarted = now;
      flashDuration = 100 + uint32_t(randf() * 100.0f);
      phaseDuration = flashDuration;
      flashCenter1 = randf();
      flashCenter2 = randf();
      const float spread = 0.5f + 0.5f * intensity;
      flashWidth1 = (0.16f + randf() * 0.2f) * spread;
      flashWidth2 = (0.12f + randf() * 0.18f) * spread;
    }
  }

  uint8_t flashR, flashG, flashB, flashW;
  unpack_rgbw(flashColor, flashR, flashG, flashB, flashW);
  const uint8_t flashPeak = std::max(std::max(flashR, flashG),
                                     std::max(flashB, flashW));
  if (flashPeak > 0) {
    flashR = uint8_t(uint16_t(flashR) * 255 / flashPeak);
    flashG = uint8_t(uint16_t(flashG) * 255 / flashPeak);
    flashB = uint8_t(uint16_t(flashB) * 255 / flashPeak);
    flashW = uint8_t(uint16_t(flashW) * 255 / flashPeak);
  }
  const uint32_t fullBrightnessFlash = pack_rgbw(flashR, flashG, flashB, flashW);
  for (size_t i = 0; i < g_ledCount; ++i) {
    const float position =
        g_ledCount > 1 ? float(i) / float(g_ledCount - 1) : 0.5f;
    const float waterMovement =
        0.5f + 0.5f * sinf(now * 0.00035f + position * 6.28318f);
    const float ambientBlend = 0.04f + 0.08f * waterMovement;

    uint8_t ambientR, ambientG, ambientB, ambientW;
    blend_rgbw_brightness(backgroundColor, waterColor, ambientBlend,
                          state.brightness, ambientR, ambientG, ambientB,
                          ambientW);
    const uint32_t ambient = pack_rgbw(
        uint8_t(ambientR * 0.22f), uint8_t(ambientG * 0.22f),
        uint8_t(ambientB * 0.22f), uint8_t(ambientW * 0.22f));

    float envelope = 0.0f;
    float cloud = 0.0f;
    if (flashActive) {
      const uint32_t elapsed = now - phaseStarted;
      const float progress = float(elapsed) / flashDuration;
      const float attack = fminf(1.0f, float(elapsed) / 18.0f);
      const float decay = expf(-fmaxf(0.0f, float(elapsed) - 18.0f) / 65.0f);
      envelope = fminf(attack, decay) * (1.0f - 0.35f * progress);

      const float distance1 = fabsf(position - flashCenter1);
      const float distance2 = fabsf(position - flashCenter2);
      const float edge1 = fminf(1.0f, distance1 / flashWidth1);
      const float edge2 = fminf(1.0f, distance2 / flashWidth2);
      const float lobe1 = 1.0f - edge1 * edge1 * (3.0f - 2.0f * edge1);
      const float lobe2 = 1.0f - edge2 * edge2 * (3.0f - 2.0f * edge2);
      cloud = fmaxf(0.2f, fmaxf(lobe1, lobe2));
    }

    const uint8_t flashBlend = uint8_t(envelope * cloud * 255.0f);
    (*g_effectBuffer)[i] =
        color_blend(ambient, fullBrightnessFlash, flashBlend);
  }
}
REGISTER_EFFECT(4, "Lightning", effect_lightning)

static uint32_t effect_color_from_string(const std::string &value) {
  if (value.empty())
    return 0;
  const char *colorString = value.c_str();
  return (uint32_t)strtoul(colorString + (colorString[0] == '#'), nullptr, 16);
}

static uint32_t effect_palette_color(float position) {
  const size_t colorCount = state.params.colors.size();
  if (colorCount == 0)
    return 0;

  position = fmodf(position, 1.0f);
  if (position < 0.0f)
    position += 1.0f;
  const float scaled = position * colorCount;
  const size_t first = size_t(scaled) % colorCount;
  const size_t second = (first + 1) % colorCount;
  const float fraction = scaled - floorf(scaled);

  uint8_t r, g, b, w;
  blend_rgbw_brightness(effect_color_from_string(state.params.colors[first]),
                        effect_color_from_string(state.params.colors[second]),
                        fraction, state.brightness, r, g, b, w);
  return pack_rgbw(r, g, b, w);
}

static uint32_t effect_scaled_color(uint32_t value) {
  uint8_t r, g, b, w;
  unpack_rgbw(value, r, g, b, w);
  scale_rgbw_brightness(r, g, b, w, state.brightness, r, g, b, w);
  return pack_rgbw(r, g, b, w);
}

void effect_kelp_forest() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const float speed =
      (state.params.speed > 0 ? state.params.speed : 50) / 255.0f;
  const float motion = now * (0.00008f + speed * 0.0002f);
  const float sway = 0.03f + 0.2f * (state.params.intensity / 255.0f);

  for (size_t i = 0; i < g_ledCount; ++i) {
    const float position = float(i) / g_ledCount;
    const float current =
        position + sway * sinf(position * 6.28318f + motion);
    (*g_effectBuffer)[i] = effect_palette_color(current);
  }
}
REGISTER_EFFECT(5, "Kelp Forest", effect_kelp_forest)

void effect_coral_reef() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const float speed =
      (state.params.speed > 0 ? state.params.speed : 50) / 255.0f;
  const float motion = now * (0.00004f + speed * 0.00012f);
  const float shimmer = state.params.intensity / 255.0f;
  const uint32_t causticColor =
      effect_scaled_color(pack_rgbw(150, 235, 255, 40));

  for (size_t i = 0; i < g_ledCount; ++i) {
    const float position = float(i) / g_ledCount;
    const float current = position * 1.5f + motion / 6.28318f;
    const float wave = 0.5f + 0.5f * sinf(position * 18.84954f - motion);
    const float bright = wave * wave * wave * wave * shimmer * 0.7f;
    const uint32_t paletteColor = effect_palette_color(current);
    (*g_effectBuffer)[i] =
        color_blend(paletteColor, causticColor, uint8_t(bright * 255.0f));
  }
}
REGISTER_EFFECT(6, "Coral Reef", effect_coral_reef)

void effect_bioluminescence() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const float speed =
      (state.params.speed > 0 ? state.params.speed : 50) / 255.0f;
  const float pulseRate = 0.00025f + speed * 0.001f;
  const float intensity = state.params.intensity / 255.0f;
  const std::string emptyColor;
  const auto &colors = state.params.colors;
  const size_t lastColor = colors.empty() ? 0 : colors.size() - 1;
  const uint32_t background =
      effect_scaled_color(effect_color_from_string(
          colors.empty() ? emptyColor : colors[0]));
  const uint32_t glow = effect_scaled_color(
      effect_color_from_string(colors.empty() ? emptyColor : colors[lastColor]));

  for (size_t i = 0; i < g_ledCount; ++i) {
    const size_t cluster = i / 8;
    const size_t clusterStart = cluster * 8;
    const size_t clusterLength =
        g_ledCount - clusterStart < 8 ? g_ledCount - clusterStart : 8;
    uint32_t hash = uint32_t(cluster) * 2654435761UL + 2246822519UL;
    hash ^= hash >> 13;
    const float center = float(clusterStart + hash % clusterLength);
    const float distance = fabsf(float(i) - center);
    const float spot = distance < 4.0f ? 1.0f - distance / 4.0f : 0.0f;
    const float pulse =
        0.5f + 0.5f * sinf(now * pulseRate + float(hash % 1000) * 0.01f);
    const float glowLevel =
        spot * spot * pulse * (0.2f + 0.8f * intensity);
    (*g_effectBuffer)[i] =
        color_blend(background, glow, uint8_t(glowLevel * 255.0f));
  }
}
REGISTER_EFFECT(7, "Bioluminescence", effect_bioluminescence)

void effect_tidal_surge() {
  if (!g_effectBuffer || g_ledCount == 0)
    return;

  const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const float speed =
      (state.params.speed > 0 ? state.params.speed : 50) / 255.0f;
  const float motion = now * (0.000025f + speed * 0.00008f);
  const float intensity = state.params.intensity / 255.0f;
  const uint32_t foamColor = effect_scaled_color(pack_rgbw(190, 245, 255, 80));

  for (size_t i = 0; i < g_ledCount; ++i) {
    const float position = float(i) / g_ledCount;
    const float tide = 0.5f + 0.5f * sinf(position * 6.28318f - motion);
    const float palettePosition = position + tide * 0.45f;
    const float foamWave =
        0.5f + 0.5f * sinf(position * 18.84954f - motion * 1.4f);
    const float foam =
        foamWave * foamWave * foamWave * foamWave * intensity * 0.8f;
    (*g_effectBuffer)[i] =
        color_blend(effect_palette_color(palettePosition), foamColor,
                    uint8_t(foam * 255.0f));
  }
}
REGISTER_EFFECT(8, "Tidal Surge", effect_tidal_surge)

// === Core rendering function ===
void renderEffectToBuffer(uint8_t effectId, const EffectParams &params,
                          std::vector<uint32_t> &buffer, size_t ledCount,
                          const std::array<uint32_t, 8> &colors,
                          size_t colorCount, uint8_t brightness) {
  if (g_renderMutex == nullptr) {
    g_renderMutex = xSemaphoreCreateMutex();
  }
  if (g_renderMutex) {
    xSemaphoreTake(g_renderMutex, portMAX_DELAY);
  }

  // Save current global state
  auto old_state = state;
  std::array<uint32_t, 8> old_color = color;
  uint8_t old_brightness = state.brightness;
  std::vector<uint32_t> *old_g_effectBuffer = g_effectBuffer;
  size_t old_g_ledCount = g_ledCount;

  // Set globals to requested values
  state.params = params;
  state.brightness = brightness;
  for (size_t i = 0; i < 8; ++i)
    color[i] = (i < colorCount) ? colors[i] : 0;
  g_effectBuffer = &buffer;
  g_ledCount = ledCount;

  if (effectId < effectRegistry.size() && effectRegistry[effectId].fn) {
    effectRegistry[effectId].fn();
  } else {
    // fallback: fill with black
    for (size_t i = 0; i < ledCount; ++i)
      buffer[i] = 0;
  }

  // Restore previous global state
  state = old_state;
  color = old_color;
  state.brightness = old_brightness;
  g_effectBuffer = old_g_effectBuffer;
  g_ledCount = old_g_ledCount;

  if (g_renderMutex) {
    xSemaphoreGive(g_renderMutex);
  }
}

uint32_t getEffectDelayMs(const EffectParams &params) {
  uint8_t speed =
      params.speed > 0 ? params.speed : 50; // Default to 50 if not set
  // Map speed (1-100) to delay (fast: 10ms, slow: 200ms)
  return 200 - ((speed - 1) * 190 / 99);
}

// === Miscellaneous functions ===
void updatePixelCount() { busManager.updatePixelCount(); }

void showStrip() { busManager.show(); }
