#include <stdint.h>
#include "effects.h"
#include "bus_manager.h"
#include "state.h"
#include "transition.h"
#include "webserver.h"
#include "display.h"
#include "colors.h"
#include "debug.h"

// Cache previous brightness for brightness-only transitions
static uint8_t previousBrightness = 0;

// Global transition state for blend_effect
EffectParams transitionPrevParams;
PendingTransitionState pendingTransition;

// Needed for effect speed control in updateLEDs
extern volatile uint8_t g_effectSpeed;

SystemState state;

extern BusManager busManager;

// Global user-selected colors (fixed size)
#include <array>
std::array<uint32_t, 8> color = {0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000};
size_t colorCount = 2;

extern Configuration config;
extern Scheduler scheduler;
extern TransitionEngine transition;
extern WebServerManager webServer;
extern void* strip;

extern int8_t lastScheduledPreset;

// Transition frame management is now handled by TransitionEngine

void applyPreset(uint8_t presetId, uint8_t brightness) {
	// Abort any ongoing transition to prevent spikes from previous effect
	transition.abortTransition();
	// Find preset by id
	auto it = std::find_if(config.presets.begin(), config.presets.end(), [presetId](const Preset& p) { return p.id == presetId; });
	if (it == config.presets.end() || !it->enabled) {
		return;
	}
	Preset& preset = *it;
	uint8_t maxBrightness = config.safety.maxBrightness;
	uint8_t safeBrightness = (brightness < maxBrightness) ? brightness : maxBrightness;
	state.brightness = safeBrightness;

	// Capture previous effect and params BEFORE applying new preset
	state.prevEffect = state.effect;
	state.prevParams = state.params;
	// Set color[] to preset colors, then update effect and params
	size_t n = preset.params.colors.size();
	colorCount = n > 0 ? n : 1;
	bool validPresetColors = true;
	for (size_t i = 0; i < n; ++i) {
		const String& hex = preset.params.colors[i];
		color[i] = parse_hex_rgbw(hex.c_str()); // Ensure W channel is always supported
		if (color[i] == 0x00000000) validPresetColors = false;
	}
	// If fewer than 8 colors, fill remaining with black
	for (size_t i = n; i < 8; ++i) {
		color[i] = 0x00000000;
	}

	// Block blend_effect if colors are black
	if (preset.effect == 1 && !validPresetColors) {
		return;
	}
	// Always use the current interpolated state as the new transition's start
	previousBrightness = transition.getCurrentBrightness();
	uint32_t prevColor1 = transition.getCurrentColor1();
	uint32_t prevColor2 = transition.getCurrentColor2();

	bool doTransition = (state.prevEffect >= 0);
	// transitionTime is set by caller (main.cpp) for scheduled presets and boot
	webServer.applyTransitionTimeLimit(state.transitionTime);

	size_t count = busManager.getPixelCount();
	// Capture previous frame for transition blending from the actual displayed LED buffer
	std::vector<uint32_t> prevFrame(count, 0);
	for (size_t i = 0; i < count; ++i) {
		prevFrame[i] = busManager.getPixelColor(i);
	}
	transition.setPreviousFrame(prevFrame);
	bool colorsChanged = false;
	// --- Generate target frame by rendering the new effect into a buffer ---
	std::vector<uint32_t> targetFrame(count, 0);
	std::array<uint32_t, 8> presetColors = {0};
	for (size_t i = 0; i < n; ++i) {
		const String& hex = preset.params.colors[i];
		presetColors[i] = parse_hex_rgbw(hex.c_str());
	}
	for (size_t i = n; i < 8; ++i) {
		presetColors[i] = 0x00000000;
	}
	size_t presetColorCount = n > 0 ? n : 1;
	uint8_t presetBrightnessHex = (brightness > 0 ? brightness : 255);
	uint8_t maxPresetBrightnessHex = config.safety.maxBrightness;
	if (presetBrightnessHex > maxPresetBrightnessHex) presetBrightnessHex = maxPresetBrightnessHex;
	renderEffectToBuffer(preset.effect, preset.params, targetFrame, count, presetColors, presetColorCount, presetBrightnessHex);
	transition.setTargetFrame(targetFrame);
	if (doTransition && state.prevEffect >= 0) {
		transition.forceCurrentBrightness(previousBrightness);
		transition.setStartBrightness(previousBrightness);
		transition.setStartColor1(prevColor1);
		transition.setStartColor2(prevColor2);
		uint32_t newColor1 = color[0];
		uint32_t newColor2 = color[1];
		transition.startEffectAndBrightnessTransition(safeBrightness, newColor1, newColor2, state.transitionTime);
	} else {
		// Boot/first transition
		transition.forceCurrentBrightness(previousBrightness);
		transition.startEffectAndBrightnessTransition(safeBrightness, color[0], color[1], state.transitionTime);
	}

	// Store new effect/params for later commit after transition
	pendingTransition.effect = preset.effect;
	pendingTransition.params = preset.params;
	pendingTransition.params.colors.clear();
	for (size_t i = 0; i < n; ++i) {
		char hex[11];
		snprintf(hex, sizeof(hex), "#%08X", color[i]);
		pendingTransition.params.colors.push_back(String(hex));
	}
	pendingTransition.preset = preset.id;
	state.power = true;
	state.inTransition = true;
	state.preset = preset.id;
	webServer.broadcastState();

}

void setPower(bool power) {
	if (state.power == power) {
		return;
	}
	state.power = power;
	digitalWrite(config.led.relayPin, power ? (config.led.relayActiveHigh ? HIGH : LOW) : (config.led.relayActiveHigh ? LOW : HIGH));
	uint8_t targetBrightness = power ? state.brightness : 0;
	// Use powerOn transition time for power changes
	state.transitionTime = config.transitionTimes.powerOn;
	webServer.applyTransitionTimeLimit(state.transitionTime);
	if (power) {
		transition.forceCurrentBrightness(state.brightness);
	}
	if (transition.getCurrentBrightness() != targetBrightness || !transition.isTransitioning()) {
		// Use current colors for effect transition
		uint32_t curColor1 = transition.getCurrentColor1();
		uint32_t curColor2 = transition.getCurrentColor2();
		transition.startEffectAndBrightnessTransition(targetBrightness, curColor1, curColor2, state.transitionTime);
	}
	webServer.broadcastState();
}

void setBrightness(uint8_t brightness) {
	uint8_t hexValue = brightness;
	// Use existing helper to apply safety brightness limit
	webServer.applyBrightnessLimit(hexValue);
	state.brightness = hexValue;
	// Use manual transition time for manual brightness changes
	state.transitionTime = config.transitionTimes.manual;
	webServer.applyTransitionTimeLimit(state.transitionTime);
	uint8_t current = transition.getCurrentBrightness();
	if (hexValue != current) {
		if (!transition.isTransitioning()) {
			transition.forceCurrentBrightness(current);
		}
		// Capture current LED state for blending
		BusNeoPixel* neo = busManager.getNeoPixelBus();
		size_t count = busManager.getPixelCount();
		std::vector<uint32_t> prevFrame(count);
		for (size_t i = 0; i < count; ++i) {
			prevFrame[i] = neo ? neo->getPixelColor(i) : 0;
		}
		transition.setPreviousFrame(prevFrame);
		// Use current colors for effect transition
		uint32_t curColor1 = transition.getCurrentColor1();
		uint32_t curColor2 = transition.getCurrentColor2();
		transition.startEffectAndBrightnessTransition(hexValue, curColor1, curColor2, state.transitionTime);
		webServer.broadcastState();
	}
}



void setEffect(uint8_t effect, const EffectParams& params) {
	state.effect = effect;
	state.params = params;
	state.params.colors.clear();
	// Only push actual preset colors, not padded black entries
	size_t n = colorCount;
	for (size_t i = 0; i < n; ++i) {
		char hex[11];
		snprintf(hex, sizeof(hex), "#%08X", color[i]);
		state.params.colors.push_back(String(hex));
	}
	BusNeoPixel* neo = busManager.getNeoPixelBus();
	if (!neo || !neo->getStrip()) return;
	extern std::vector<EffectRegistryEntry> effectRegistry;
	if (effect < effectRegistry.size() && effectRegistry[effect].fn) {
		// Update global effect speed if present in params
		if (params.speed > 0) {
			extern volatile uint8_t g_effectSpeed;
			g_effectSpeed = (params.speed * 254) / 100 + 1;
		}
		effectRegistry[effect].fn();
	}
}

// Call this when user changes color from UI/API
void setUserColor(const uint32_t* newColor, size_t count) {
	colorCount = count;
	state.params.colors.clear();
	for (size_t i = 0; i < 8; ++i) {
		char hex[11];
		snprintf(hex, sizeof(hex), "#%08X", color[i]);
		state.params.colors.push_back(String(hex));
	}
	// Use effect transition time for color/effect changes
	state.transitionTime = config.transitionTimes.manual;
	setEffect(state.effect, state.params);
}

void updateLEDs() {
	BusNeoPixel* neo = busManager.getNeoPixelBus();
	if (!neo || !neo->getStrip()) return;
	if (!state.power) {
		busManager.turnOffLEDs();
		state.inTransition = false;
		state.brightness = 0;
		digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? LOW : HIGH);
		return;
	}
	size_t count = busManager.getPixelCount();
	static bool pendingCommit = false;
	extern std::vector<EffectRegistryEntry> effectRegistry;
	if (transition.isTransitioning()) {
		pendingCommit = true;
		float progress = float(millis() - transition.getStartTime()) / float(transition.getDuration());
		if (progress > 1.0f) progress = 1.0f;
		progress = progress * progress * (3.0f - 2.0f * progress); // smoothstep

		// Color phase fraction (should match transition engine)
		float colorFrac = transition.getEffectTransitionFraction();
		float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;

		// Determine if this is a brightness-only transition
		bool brightnessOnly = (pendingTransition.effect == state.effect && pendingTransition.params.colors == state.params.colors);

		std::vector<uint32_t> prevFrame(count, 0);
		std::vector<uint32_t> nextFrame(count, 0);
		if (brightnessOnly) {
			// For brightness-only, render both frames with the same effect/colors, but different brightness
			std::array<uint32_t, 8> colors = {0};
			for (size_t i = 0; i < pendingTransition.params.colors.size() && i < 8; ++i) {
				const String& hex = pendingTransition.params.colors[i];
				colors[i] = (uint32_t)strtoul(hex.c_str() + (hex[0] == '#' ? 1 : 0), nullptr, 16);
			}
			size_t colorCount = pendingTransition.params.colors.size() > 0 ? pendingTransition.params.colors.size() : 1;
			uint8_t prevBrightness = transition.getCurrentBrightness();
			uint8_t nextBrightness = transition.getTargetBrightness();
			renderEffectToBuffer(pendingTransition.effect, pendingTransition.params, prevFrame, count, colors, colorCount, prevBrightness);
			renderEffectToBuffer(pendingTransition.effect, pendingTransition.params, nextFrame, count, colors, colorCount, nextBrightness);
		} else {
			if (state.prevEffect == 0) {
				// Solid effect: use captured buffer for previous frame
				prevFrame = transition.getPreviousFrame();
			} else {
				// Animated effect: re-render previous frame
				std::array<uint32_t, 8> prevColors = {0};
				for (size_t i = 0; i < state.prevParams.colors.size() && i < 8; ++i) {
					const String& hex = state.prevParams.colors[i];
					prevColors[i] = (uint32_t)strtoul(hex.c_str() + (hex[0] == '#' ? 1 : 0), nullptr, 16);
				}
				size_t prevColorCount = state.prevParams.colors.size() > 0 ? state.prevParams.colors.size() : 1;
				uint8_t prevBrightness = transition.getCurrentBrightness();
				renderEffectToBuffer(state.prevEffect, state.prevParams, prevFrame, count, prevColors, prevColorCount, prevBrightness);
			}
			// Always re-render next frame (target effect)
			std::array<uint32_t, 8> nextColors = {0};
			for (size_t i = 0; i < pendingTransition.params.colors.size() && i < 8; ++i) {
				const String& hex = pendingTransition.params.colors[i];
				nextColors[i] = (uint32_t)strtoul(hex.c_str() + (hex[0] == '#' ? 1 : 0), nullptr, 16);
			}
			size_t nextColorCount = pendingTransition.params.colors.size() > 0 ? pendingTransition.params.colors.size() : 1;
			uint8_t nextBrightness = transition.getTargetBrightness();
			renderEffectToBuffer(pendingTransition.effect, pendingTransition.params, nextFrame, count, nextColors, nextColorCount, nextBrightness);
		}

		// Blend the two frames
		std::vector<uint32_t> blended(count, 0);
		for (size_t i = 0; i < count; ++i) {
			uint32_t prev = prevFrame[i];
			uint32_t next = nextFrame[i];
			uint8_t r, g, b, w;
			// Use colorProgress for color blending, progress for brightness
			float blendFactor = colorProgress;
			blend_rgbw_brightness(prev, next, blendFactor, 255, r, g, b, w);
			blended[i] = pack_rgbw(r, g, b, w); // RRGGBBWW
		}
		bool allZero = true;
		for (size_t i = 0; i < count; ++i) {
			uint32_t c = blended[i];
			if (c != 0) allZero = false;
		}
		for (size_t i = 0; i < count; ++i) {
			uint32_t c = blended[i];
			uint8_t r, g, b, w;
			unpack_rgbw(c, r, g, b, w);
			busManager.setPixelColor(i, pack_rgbw(r, g, b, w));
		}
		busManager.show();
	} else {
		// Only commit pendingTransition after transition completes
		if (pendingCommit) {
			state.effect = pendingTransition.effect;
			state.params = pendingTransition.params;
			state.preset = pendingTransition.preset;
			// Restore brightness and color before rendering solid effect
			state.brightness = transition.getTargetBrightness();
			if (state.effect == 0 && state.params.colors.size() > 0) {
				color[0] = parse_hex_rgbw(state.params.colors[0].c_str()); // Use parse_hex_rgbw for color[0] assignment
			}
			setEffect(state.effect, state.params);
			pendingCommit = false;
			transition.clearFrames();
		}
		// Only update state and show LEDs after transition completes
		uint8_t currentBrightness = transition.getCurrentBrightness();
		state.inTransition = false;
		state.brightness = currentBrightness;

		// --- ANIMATION: render effect every frame ---
		std::vector<uint32_t> animFrame(count, 0);
		std::array<uint32_t, 8> animColors = {0};
		for (size_t i = 0; i < state.params.colors.size() && i < 8; ++i) {
			const String& hex = state.params.colors[i];
			animColors[i] = parse_hex_rgbw(hex.c_str()); // Use parse_hex_rgbw for animColors
		}
		size_t animColorCount = state.params.colors.size() > 0 ? state.params.colors.size() : 1;
		uint8_t animBrightness = currentBrightness;
		renderEffectToBuffer(state.effect, state.params, animFrame, count, animColors, animColorCount, animBrightness);
		for (size_t i = 0; i < count; ++i) {
			uint32_t c = animFrame[i];
			uint8_t r, g, b, w;
			unpack_rgbw(c, r, g, b, w);
			busManager.setPixelColor(i, pack_rgbw(r, g, b, w));
		}
		busManager.show();
		// Only set relay if power is on
		if (state.power) {
			digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? HIGH : LOW);
		}
	}
}
