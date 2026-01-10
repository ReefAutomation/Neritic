#include <WS2812FX.h>
#include "state.h"
#include "transition.h"
#include "webserver.h"
SystemState state;

extern Configuration config;
extern Scheduler scheduler;
extern TransitionEngine transition;
extern WebServerManager webServer;
extern WS2812FX* strip;
extern int8_t lastScheduledPreset;

void applyPreset(uint8_t presetId, bool setManualOverride) {
	if (presetId >= config.getPresetCount() || !config.presets[presetId].enabled) {
		debugPrintln("Invalid preset ID");
		return;
	}
	Preset& preset = config.presets[presetId];
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
	setEffect(preset.effect, preset.params);
	state.currentPreset = presetId;
	state.power = true;
	state.inTransition = true;
	webServer.broadcastState();
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
	if (strip) {
		strip->setMode(effect);
		strip->setColor(params.color1);
		// Convert percent (0–100) to WS2812FX speed (0–65535, 0=fastest)
		uint16_t ws_speed = 65535 - (params.speed * 65535 / 100);
		strip->setSpeed(ws_speed);
		// If your WS2812FX library supports intensity, set it here:
		#ifdef WS2812FX_HAS_INTENSITY
		strip->setIntensity(params.intensity);
		#endif
	}
}

void updateLEDs() {
	if (!state.power) {
		strip->setBrightness(0);
		strip->service();
		state.inTransition = false;
		state.brightness = 0;
		digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? LOW : HIGH);
		return;
	}
	uint8_t currentBrightness = transition.getCurrentBrightness();
	uint8_t prevBrightness = state.brightness;
	strip->setBrightness(currentBrightness);
	strip->service();
	state.inTransition = false;
	state.brightness = currentBrightness;
	digitalWrite(config.led.relayPin, config.led.relayActiveHigh ? HIGH : LOW);
}
