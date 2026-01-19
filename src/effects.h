#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include "config.h"
#include <vector>
#include <cstdint>
#include <array>
#include <cstddef>
#include "state.h"

// Render the given effect and params into a buffer (does not update LEDs)
void renderEffectToBuffer(uint8_t effectId, const EffectParams& params, std::vector<uint32_t>& buffer, size_t ledCount, const std::array<uint32_t, 8>& colors, size_t colorCount, uint8_t brightness);


// All effect frame generators now take no parameters and use global buffer/ledCount
typedef void (*EffectFrameGen)();
struct EffectRegistryEntry {
	uint8_t id;
	const char* name;
	EffectFrameGen fn;
};
extern std::vector<EffectRegistryEntry> effectRegistry;

struct PendingTransitionState {
	uint8_t effect = 0;
	EffectParams params;
	uint8_t preset = 0;
};

// Centralized effect speed to delay mapping
uint32_t getEffectDelayMs(const EffectParams& params);

extern std::array<uint32_t, 8> color;
extern size_t colorCount;
extern void* strip;

#ifdef __cplusplus
extern "C" {
#endif

void updatePixelCount();

#ifdef __cplusplus
}
#endif


// Registration helper macro for effect frame generators
#define REGISTER_EFFECT(id, name, fn) \
	namespace { \
		struct fn##_registrar { \
			fn##_registrar() { \
				effectRegistry.push_back({id, name, fn}); \
			}\
		} \
		fn##_registrar_instance; \
	}

#endif // EFFECTS_H
