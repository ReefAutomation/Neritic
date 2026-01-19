
#include <Arduino.h>
#include "transition.h"
#include "bus_manager.h"
#include "colors.h"
void TransitionEngine::abortTransition() {
    _active = false;
    _phase = Phase::None;
    clearFrames();
}
void TransitionEngine::startEffectAndBrightnessTransition(uint8_t targetBrightness, uint32_t targetColor1, uint32_t targetColor2, uint32_t duration) {
    // Start color transition first, then brightness after color transition completes
    // Start combined transition: brightness always over full duration, color only for initial fraction
    _phase = Phase::Brightness;
    _pendingBrightnessTransition = false;
    _startBrightness = _currentBrightness;
    _targetBrightness = targetBrightness;
    _startColor1 = _currentColor1;
    _targetColor1 = targetColor1;
    _startColor2 = _currentColor2;
    _targetColor2 = targetColor2;
    _startTime = millis();
    _duration = duration;
    _active = true;
}
// Frame blending API
void TransitionEngine::setPreviousFrame(const std::vector<uint32_t>& frame) {
    this->previousFrame = frame;
}
void TransitionEngine::setTargetFrame(const std::vector<uint32_t>& frame) {
    this->targetFrame = frame;
}
void TransitionEngine::clearFrames() {
    previousFrame.clear();
    targetFrame.clear();
}
std::vector<uint32_t> TransitionEngine::getBlendedFrame(float progress, bool brightnessOnly) {
    std::vector<uint32_t> blended;
    size_t count = previousFrame.size();
    blended.resize(count);
    if (brightnessOnly) {
        uint8_t startBrightness = _startBrightness;
        uint8_t endBrightness = _targetBrightness;
        uint8_t blendedBrightness = (uint8_t)(startBrightness * (1.0f - progress) + endBrightness * progress);
        for (size_t i = 0; i < count; ++i) {
            uint32_t colorVal = previousFrame[i];
            uint8_t r, g, b, w;
            unpack_rgbw(colorVal, r, g, b, w);
            scale_rgbw_brightness(r, g, b, w, blendedBrightness, r, g, b, w);
            blended[i] = pack_rgbw(r, g, b, w);
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            uint32_t prev = previousFrame[i];
            uint32_t next = targetFrame[i];
            uint8_t r, g, b, w;
            blend_rgbw_brightness(prev, next, progress, 255, r, g, b, w);
            blended[i] = pack_rgbw(r, g, b, w);
        }
    }
    bool allZero = true;
    for (size_t i = 0; i < blended.size(); ++i) {
        if (blended[i] != 0) {
            allZero = false;
            break;
        }
    }
    return blended;
}
void TransitionEngine::forceCurrentBrightness(uint8_t value) {
    _currentBrightness = value;
}

void TransitionEngine::forceCurrentColor(uint32_t color1, uint32_t color2) {
    _currentColor1 = color1;
    _currentColor2 = color2;
}

TransitionEngine::TransitionEngine() {}

void TransitionEngine::startTransition(uint8_t targetBrightness, uint32_t duration) {
    // Always start a transition, even if brightness does not change, to allow color transitions
    _startBrightness = _currentBrightness;
    _targetBrightness = targetBrightness;
    _startTime = millis();
    _duration = duration < ABSOLUTE_MIN_TRANSITION ? ABSOLUTE_MIN_TRANSITION : duration;
    _active = true;
}

void TransitionEngine::startColorTransition(uint32_t targetColor1, uint32_t targetColor2, uint32_t duration) {
    _startColor1 = _currentColor1;
    _targetColor1 = targetColor1;
    _startColor2 = _currentColor2;
    _targetColor2 = targetColor2;
}

void TransitionEngine::update() {
    if (!_active) {
        _phase = Phase::None;
        return;
    }

    uint32_t elapsed = millis() - _startTime;
    if (elapsed >= _duration) {
        _currentBrightness = _targetBrightness;
        _currentColor1 = _targetColor1;
        _currentColor2 = _targetColor2;
        _active = false;
        _phase = Phase::None;
        return;
    }

    // Calculate progress (0.0 to 1.0)
    float progress = (float)elapsed / (float)_duration;
    progress = progress * progress * (3.0 - 2.0 * progress);

    // Brightness always transitions over full duration
    _currentBrightness = interpolate(_startBrightness, _targetBrightness, progress);

    // Use transitionTimes.effect to determine the fraction of the transition for effect/color
    extern Configuration config;
    float colorFrac = 1.0f;
    if (config.transitionTimes.effect > 0 && _duration > 0) {
        colorFrac = float(config.transitionTimes.effect) / float(_duration);
        if (colorFrac > 1.0f) colorFrac = 1.0f;
        if (colorFrac < 0.01f) colorFrac = 0.01f;
    }
    float colorProgress = (progress < colorFrac) ? (progress / colorFrac) : 1.0f;
    if (colorProgress < 1.0f) {
        _currentColor1 = interpolateColor(_startColor1, _targetColor1, colorProgress);
        _currentColor2 = interpolateColor(_startColor2, _targetColor2, colorProgress);
    } else {
        _currentColor1 = _targetColor1;
        _currentColor2 = _targetColor2;
    }
}

bool TransitionEngine::isTransitioning() {
    return _active;
}

uint8_t TransitionEngine::getCurrentBrightness() {
    return _currentBrightness;
}

uint32_t TransitionEngine::getCurrentColor1() {
    return _currentColor1;
}

uint32_t TransitionEngine::getCurrentColor2() {
    return _currentColor2;
}

uint8_t TransitionEngine::interpolate(uint8_t start, uint8_t target, float progress) {
    return start + (uint8_t)ceilf((float)(target - start) * progress);
}

uint32_t TransitionEngine::interpolateColor(uint32_t start, uint32_t target, float progress) {
    uint8_t r, g, b, w;
    blend_rgbw_brightness(start, target, progress, 255, r, g, b, w);
    return pack_rgbw(r, g, b, w);
}
