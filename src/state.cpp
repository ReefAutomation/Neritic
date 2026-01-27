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

static bool hasValidPresetColors(const std::vector<String>& presetColorsVec) {
	for (const auto& hex : presetColorsVec) {
		if (parse_hex_rgbw(hex.c_str()) == 0x00000000) return false;
	}
	return true;
}

static void captureCurrentBusFrame(std::vector<uint32_t>& frame) {
	size_t count = busManager.getPixelCount();
	frame.resize(count);
	for (size_t i = 0; i < count; ++i) {
		frame[i] = busManager.getPixelColor(i);
	}
}

static void fillArrayFromPresetColors(const std::vector<String>& presetColorsVec, std::array<uint32_t, 8>& arr) {
	size_t n = presetColorsVec.size();
	for (size_t i = 0; i < n && i < 8; ++i) {
		arr[i] = parse_hex_rgbw(presetColorsVec[i].c_str());
	}
	for (size_t i = n; i < 8; ++i) {
		arr[i] = 0x00000000;
	}
}

static void setPendingTransitionFromPreset(const Preset& preset, size_t n) {
	pendingTransition.effect = preset.effect;
	pendingTransition.params = preset.params;
	pendingTransition.params.colors.clear();
	for (size_t i = 0; i < n; ++i) {
		char hex[11];
		snprintf(hex, sizeof(hex), "#%08X", color[i]);
		pendingTransition.params.colors.push_back(String(hex));
	}
	pendingTransition.preset = preset.id;
}

void applyPreset(uint8_t presetId, uint8_t brightness) {
	transition.abortTransition();
	auto it = std::find_if(config.presets.begin(), config.presets.end(), [presetId](const Preset& p) { return p.id == presetId; });
	if (it == config.presets.end() || !it->enabled) return;
	Preset& preset = *it;
	uint8_t safeBrightness = std::min(brightness, config.safety.maxBrightness);
	state.brightness = safeBrightness;

	state.prevEffect = state.effect;
	state.prevParams = state.params;
	colorCount = preset.params.colors.size() > 0 ? preset.params.colors.size() : 1;
	fillArrayFromPresetColors(preset.params.colors, color);
	if (preset.effect == 1 && !hasValidPresetColors(preset.params.colors)) return;

	previousBrightness = transition.getCurrentBrightness();
	uint32_t prevColor1 = transition.getCurrentColor1();
	uint32_t prevColor2 = transition.getCurrentColor2();
	bool doTransition = (state.prevEffect >= 0);
	webServer.applyTransitionTimeLimit(state.transitionTime);

	size_t count = busManager.getPixelCount();
	std::vector<uint32_t> prevFrame;
	captureCurrentBusFrame(prevFrame);
	transition.setPreviousFrame(prevFrame);

	std::vector<uint32_t> targetFrame(count, 0);
	std::array<uint32_t, 8> presetColors = {0};
	fillArrayFromPresetColors(preset.params.colors, presetColors);
	size_t presetColorCount = preset.params.colors.size() > 0 ? preset.params.colors.size() : 1;
	uint8_t presetBrightnessHex = (brightness > 0 ? brightness : 255);
	presetBrightnessHex = std::min(presetBrightnessHex, config.safety.maxBrightness);
	renderEffectToBuffer(preset.effect, preset.params, targetFrame, count, presetColors, presetColorCount, presetBrightnessHex);
	transition.setTargetFrame(targetFrame);

	if (doTransition) {
		transition.forceCurrentBrightness(previousBrightness);
		transition.setStartBrightness(previousBrightness);
		transition.setStartColor1(prevColor1);
		transition.setStartColor2(prevColor2);
		transition.startEffectAndBrightnessTransition(safeBrightness, color[0], color[1], state.transitionTime);
	} else {
		transition.forceCurrentBrightness(previousBrightness);
		transition.startEffectAndBrightnessTransition(safeBrightness, color[0], color[1], state.transitionTime);
	}

	setPendingTransitionFromPreset(preset, preset.params.colors.size());
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

static void captureCurrentFrameForTransition() {
	BusNeoPixel* neo = busManager.getNeoPixelBus();
	size_t count = busManager.getPixelCount();
	std::vector<uint32_t> prevFrame(count);
	for (size_t i = 0; i < count; ++i) {
		prevFrame[i] = neo ? neo->getPixelColor(i) : 0;
	}
	transition.setPreviousFrame(prevFrame);
}

void setBrightness(uint8_t brightness) {
	webServer.applyBrightnessLimit(brightness);
	state.brightness = brightness;
	state.transitionTime = config.transitionTimes.manual;
	webServer.applyTransitionTimeLimit(state.transitionTime);
	uint8_t current = transition.getCurrentBrightness();
	if (brightness == current) return;
	if (!transition.isTransitioning()) {
		transition.forceCurrentBrightness(current);
	}
	captureCurrentFrameForTransition();
	transition.startEffectAndBrightnessTransition(
		brightness,
		transition.getCurrentColor1(),
		transition.getCurrentColor2(),
		state.transitionTime
	);
	webServer.broadcastState();
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
	// Debug: print speed value to confirm it's 8-bit
	printf("[DEBUG] setEffect: state.params.speed = %u\n", state.params.speed);
	BusNeoPixel* neo = busManager.getNeoPixelBus();
	if (!neo || !neo->getStrip()) return;
	extern std::vector<EffectRegistryEntry> effectRegistry;
	if (effect < effectRegistry.size() && effectRegistry[effect].fn) {
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

// Helper to convert color hex strings to array
static std::array<uint32_t, 8> getColorsFromParams(const std::vector<String>& colorsVec) {
	std::array<uint32_t, 8> colors = {0};
	for (size_t i = 0; i < colorsVec.size() && i < 8; ++i) {
		const String& hex = colorsVec[i];
		colors[i] = (uint32_t)strtoul(hex.c_str() + (hex[0] == '#' ? 1 : 0), nullptr, 16);
	}
	return colors;
}

static void renderFrameToBus(const std::vector<uint32_t>& frame) {
	for (size_t i = 0; i < frame.size(); ++i) {
		uint32_t c = frame[i];
		uint8_t r, g, b, w;
		unpack_rgbw(c, r, g, b, w);
		busManager.setPixelColor(i, pack_rgbw(r, g, b, w));
	}
	busManager.show();
}

static void blendFrames(const std::vector<uint32_t>& prevFrame, const std::vector<uint32_t>& nextFrame, float blendFactor, std::vector<uint32_t>& blended) {
	for (size_t i = 0; i < blended.size(); ++i) {
		uint32_t prev = prevFrame[i];
		uint32_t next = nextFrame[i];
		uint8_t r, g, b, w;
		blend_rgbw_brightness(prev, next, blendFactor, 255, r, g, b, w);
		blended[i] = pack_rgbw(r, g, b, w);
	}
}

// --- updateLEDs helpers ---
static void renderTransitionFrame(size_t count, float colorProgress, bool brightnessOnly) {
	std::vector<uint32_t> prevFrame(count, 0);
	std::vector<uint32_t> nextFrame(count, 0);
	if (brightnessOnly) {
		auto colors = getColorsFromParams(pendingTransition.params.colors);
		size_t colorCount = pendingTransition.params.colors.size() > 0 ? pendingTransition.params.colors.size() : 1;
		uint8_t prevBrightness = transition.getCurrentBrightness();
		uint8_t nextBrightness = transition.getTargetBrightness();
		renderEffectToBuffer(pendingTransition.effect, pendingTransition.params, prevFrame, count, colors, colorCount, prevBrightness);
		renderEffectToBuffer(pendingTransition.effect, pendingTransition.params, nextFrame, count, colors, colorCount, nextBrightness);
	} else {
		if (state.prevEffect == 0) {
			prevFrame = transition.getPreviousFrame();
		} else {
			auto prevColors = getColorsFromParams(state.prevParams.colors);
			size_t prevColorCount = state.prevParams.colors.size() > 0 ? state.prevParams.colors.size() : 1;
			uint8_t prevBrightness = transition.getCurrentBrightness();
			renderEffectToBuffer(state.prevEffect, state.prevParams, prevFrame, count, prevColors, prevColorCount, prevBrightness);
		}
		auto nextColors = getColorsFromParams(pendingTransition.params.colors);
		size_t nextColorCount = pendingTransition.params.colors.size() > 0 ? pendingTransition.params.colors.size() : 1;
		uint8_t nextBrightness = transition.getTargetBrightness();
		renderEffectToBuffer(pendingTransition.effect, pendingTransition.params, nextFrame, count, nextColors, nextColorCount, nextBrightness);
	}
	std::vector<uint32_t> blended(count, 0);
	blendFrames(prevFrame, nextFrame, colorProgress, blended);
	renderFrameToBus(blended);
}

static void commitPendingTransition() {
	state.effect = pendingTransition.effect;
	state.params = pendingTransition.params;
	state.preset = pendingTransition.preset;
	state.brightness = transition.getTargetBrightness();
	if (state.effect == 0 && state.params.colors.size() > 0) {
		color[0] = parse_hex_rgbw(state.params.colors[0].c_str());
	}
	setEffect(state.effect, state.params);
	transition.clearFrames();
}

static void renderAnimationFrame(size_t count, uint8_t brightness) {
	std::vector<uint32_t> animFrame(count, 0);
	auto animColors = getColorsFromParams(state.params.colors);
	size_t animColorCount = state.params.colors.size() > 0 ? state.params.colors.size() : 1;
	renderEffectToBuffer(state.effect, state.params, animFrame, count, animColors, animColorCount, brightness);
	renderFrameToBus(animFrame);
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
	if (transition.isTransitioning()) {
		pendingCommit = true;
		float progress = float(millis() - transition.getStartTime()) / float(transition.getDuration());
		if (progress > 1.0f) progress = 1.0f;
		progress = progress * progress * (3.0f - 2.0f * progress); // smoothstep
		float colorFrac = transition.getEffectTransitionFraction();
		float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;
		bool brightnessOnly = (pendingTransition.effect == state.effect && pendingTransition.params.colors == state.params.colors);
		renderTransitionFrame(count, colorProgress, brightnessOnly);
	} else {
		if (pendingCommit) {
			commitPendingTransition();
			pendingCommit = false;
		}
		uint8_t currentBrightness = transition.getCurrentBrightness();
		state.inTransition = false;
		state.brightness = currentBrightness;
		renderAnimationFrame(count, currentBrightness);
		if (state.power) {
			digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? HIGH : LOW);
		}
	}
}
