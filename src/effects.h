#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include "config.h"
#include <vector>

struct EffectRegistryEntry {
	const char* name;
	uint16_t (*handler)();
};

// Registry accessor and registration function (must be declared before macro)
const std::vector<EffectRegistryEntry>& getEffectRegistry();

// Registration macro (uses static object constructor)
#define REGISTER_EFFECT(NAME, FUNC) \
	namespace { \
	struct _AutoReg_##FUNC { \
		_AutoReg_##FUNC() { \
			_registerEffect(NAME, FUNC); \
		} \
	}; \
	static _AutoReg_##FUNC _autoReg_##FUNC; \
	}

void _registerEffect(const char* name, uint16_t (*handler)());

// Solid color effect
uint16_t solid_effect();
// Blend effect mimicking WLED mode_blends
uint16_t blend_effect();
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

#endif // EFFECTS_H
