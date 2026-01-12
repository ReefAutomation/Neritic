#include "bus_manager.h"
#include "state.h"
#include "effects.h"
#include "transition.h"
#include "webserver.h"
#include "display.h"
#include "debug.h"

SystemState state;

extern BusManager busManager;

// Global user-selected colors (fixed size)
#include <array>
std::array<uint32_t, 8> color = {0x0000FF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF};
size_t colorCount = 2;

extern Configuration config;
extern Scheduler scheduler;
extern TransitionEngine transition;
extern WebServerManager webServer;
extern void* strip;
extern int8_t lastScheduledPreset;

void applyPreset(uint8_t presetId, bool setManualOverride) {
	// Find preset by id
	auto it = std::find_if(config.presets.begin(), config.presets.end(), [presetId](const Preset& p) { return p.id == presetId; });
	if (it == config.presets.end() || !it->enabled) {
		debugPrintln("Invalid preset ID");
		return;
	}
	Preset& preset = *it;
	uint8_t timerBrightnessPercent = 100;
	for (size_t i = 0; i < config.timers.size(); i++) {
		if (config.timers[i].presetId == presetId && config.timers[i].enabled) {
			timerBrightnessPercent = config.timers[i].brightness;
			break;
		}
	}
	uint8_t brightnessValue = (uint8_t)((timerBrightnessPercent / 100.0) * 255);
	uint8_t safeBrightness = min(brightnessValue, config.safety.maxBrightness);
	uint32_t transTime = state.transitionTime;
	if (transTime < config.safety.minTransitionTime) {
		transTime = config.safety.minTransitionTime;
	}
	transition.startTransition(safeBrightness, transTime);

	// Set color[] to preset colors, then update effect and params
	size_t n = preset.params.colors.size();
	colorCount = n > 0 ? n : 1; // Reset colorCount to preset color count (at least 1)
	for (size_t i = 0; i < 8; ++i) {
		if (i < n) {
			const String& hex = preset.params.colors[i];
			color[i] = (uint32_t)strtoul(hex.c_str() + (hex[0] == '#' ? 1 : 0), nullptr, 16);
		} else {
			color[i] = (i == 0) ? 0x0000FF : 0x00FFFF;
		}
	}
	state.effect = preset.effect;
	state.params = preset.params;
	state.params.colors.clear();
	for (size_t i = 0; i < n; ++i) {
		char hex[10];
		snprintf(hex, sizeof(hex), "#%06X", color[i] & 0xFFFFFF);
		state.params.colors.push_back(String(hex));
	}
	state.currentPreset = preset.id;
	state.power = true;
	state.inTransition = true;
	webServer.broadcastState();
	// No need to reset userColor1/2 here
	setEffect(state.effect, state.params);
}

void setPower(bool power) {
	if (state.power == power) {
		return;
	}
	state.power = power;
	digitalWrite(config.led.relayPin, power ? (config.led.relayActiveHigh ? HIGH : LOW) : (config.led.relayActiveHigh ? LOW : HIGH));
	uint8_t targetBrightness = power ? state.brightness : 0;
	uint32_t transTime = state.transitionTime;
	if (transTime < config.safety.minTransitionTime) {
		transTime = config.safety.minTransitionTime;
	}
	if (power) {
		transition.forceCurrentBrightness(state.brightness);
	}
	if (transition.getCurrentBrightness() != targetBrightness || !transition.isTransitioning()) {
		transition.startTransition(targetBrightness, transTime);
	}
	webServer.broadcastState();

}

void setBrightness(uint8_t brightness) {
	brightness = min(brightness, config.safety.maxBrightness);
	uint32_t transTime = state.transitionTime;
	if (transTime < config.safety.minTransitionTime) {
		transTime = config.safety.minTransitionTime;
	}
	uint8_t current = transition.getCurrentBrightness();
	if (brightness != current) {
		if (!transition.isTransitioning()) {
			transition.forceCurrentBrightness(state.brightness);
		}
		transition.startTransition(brightness, transTime);
		webServer.broadcastState();
	}
}



void setEffect(uint8_t effect, const EffectParams& params) {
	state.effect = effect;
	state.params = params;
	state.params.colors.clear();
	size_t n = 0;
	for (size_t i = 0; i < 8; ++i) {
		// Only push colors that are actually set (not default)
		if (i == 0 || color[i] != 0x00FFFF) {
			char hex[10];
			snprintf(hex, sizeof(hex), "#%06X", color[i] & 0xFFFFFF);
			state.params.colors.push_back(String(hex));
			n++;
		} else {
			break;
		}
	}
	BusNeoPixel* neo = busManager.getNeoPixelBus();
	if (!neo || !neo->getStrip()) return;
	const auto& reg = getEffectRegistry();
	if (effect < reg.size() && reg[effect].handler) {
		// Update global effect speed if present in params
		   if (params.speed > 0) {
			   extern volatile uint8_t g_effectSpeed;
			   // Map UI speed (1-100) to WLED effect speed (1-255)
			   g_effectSpeed = (params.speed * 254) / 100 + 1;
		   }
		reg[effect].handler();
	}
}

// Call this when user changes color from UI/API
void setUserColor(const uint32_t* newColor, size_t count) {
	colorCount = count;
	debugPrint("[setUserColor] count: ");
	debugPrintln((int)count);
	for (size_t i = 0; i < count; ++i) {
		debugPrint("[setUserColor] newColor[");
		debugPrint((int)i);
		debugPrint("] = 0x");
		debugPrintln((unsigned int)newColor[i], HEX);
	}
	// Fill color array with new values, pad as needed
	for (size_t i = 0; i < 8; ++i) {
		if (i < count) {
			color[i] = newColor[i];
		} else {
			color[i] = (i == 0) ? 0x0000FF : 0x00FFFF;
		}
	}
	debugPrint("[setUserColor] color: ");
	for (size_t i = 0; i < 8; ++i) {
		debugPrint("["); debugPrint((int)i); debugPrint("] = 0x"); debugPrintln((unsigned int)color[i], HEX);
	}
	state.params.colors.clear();
	for (size_t i = 0; i < 8; ++i) {
		char hex[10];
		snprintf(hex, sizeof(hex), "#%06X", color[i] & 0xFFFFFF);
		state.params.colors.push_back(String(hex));
	}
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
	if (state.effect == 1) {
		blend_effect();
	}
	uint8_t currentBrightness = transition.getCurrentBrightness();
	uint8_t prevBrightness = state.brightness;
	state.inTransition = false;
	state.brightness = currentBrightness;
	digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? HIGH : LOW);
}
