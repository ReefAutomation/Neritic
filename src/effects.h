#ifndef EFFECTS_H
#define EFFECTS_H

#include "config.h"
#include "state.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

void renderEffectToBuffer(uint8_t effectId, const EffectParams &params,
                          std::vector<uint32_t> &buffer, size_t ledCount,
                          const std::array<uint32_t, 8> &colors,
                          size_t colorCount, uint8_t brightness);

// Expose the current effect buffer for live preview (used by webserver)
extern std::vector<uint32_t> *g_effectBuffer;
// Expose the last output frame for live bar (set in updateLEDs)
extern std::vector<uint32_t> *g_outputFramePtr;

// All effect frame generators now take no parameters and use global
// buffer/ledCount
typedef void (*EffectFrameGen)();
struct EffectRegistryEntry {
  uint8_t id;
  const char *name;
  EffectFrameGen fn;
};
extern std::vector<EffectRegistryEntry> effectRegistry;

// Centralized effect speed to delay mapping
uint32_t getEffectDelayMs(const EffectParams &params);

extern std::array<uint32_t, 8> color;
extern size_t colorCount;
extern void *strip;

#ifdef __cplusplus
extern "C" {
#endif

void updatePixelCount();

#ifdef __cplusplus
}
#endif

// Registration helper macro for effect frame generators
#define REGISTER_EFFECT(id, name, fn)                                          \
  namespace {                                                                  \
  struct fn##_registrar {                                                      \
    fn##_registrar() { effectRegistry.push_back({id, name, fn}); }             \
  } fn##_registrar_instance;                                                   \
  }

#endif // EFFECTS_H
