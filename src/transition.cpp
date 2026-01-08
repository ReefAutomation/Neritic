
#include <Arduino.h>
#include "transition.h"

void TransitionEngine::forceCurrentBrightness(uint8_t value) {
    _currentBrightness = value;
}

TransitionEngine::TransitionEngine() {}

void TransitionEngine::startTransition(uint8_t targetBrightness, uint32_t duration) {
    _startBrightness = _currentBrightness;
    _targetBrightness = targetBrightness;
    _startTime = millis();
    _duration = max(duration, (uint32_t)ABSOLUTE_MIN_TRANSITION);
    _active = true;
}

void TransitionEngine::startColorTransition(uint32_t targetColor1, uint32_t targetColor2, uint32_t duration) {
    _startColor1 = _currentColor1;
    _targetColor1 = targetColor1;
    _startColor2 = _currentColor2;
    _targetColor2 = targetColor2;
    _startTime = millis();
    _duration = max(duration, (uint32_t)ABSOLUTE_MIN_TRANSITION);
    _active = true;
}

void TransitionEngine::update() {
    if (!_active) return;

    uint32_t elapsed = millis() - _startTime;
    if (elapsed >= _duration) {
        // Transition complete
        
        _currentBrightness = _targetBrightness;
        _currentColor1 = _targetColor1;
        _currentColor2 = _targetColor2;
        _active = false;
        return;
    }

    // Calculate progress (0.0 to 1.0)
    float progress = (float)elapsed / (float)_duration;

    // Smooth easing (ease-in-out)
    progress = progress * progress * (3.0 - 2.0 * progress);

    uint8_t prevBrightness = _currentBrightness;
    // Interpolate values
    _currentBrightness = interpolate(_startBrightness, _targetBrightness, progress);
    _currentColor1 = interpolateColor(_startColor1, _targetColor1, progress);
    _currentColor2 = interpolateColor(_startColor2, _targetColor2, progress);
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
    return start + (uint8_t)((float)(target - start) * progress);
}

uint32_t TransitionEngine::interpolateColor(uint32_t start, uint32_t target, float progress) {
    uint8_t sr = (start >> 16) & 0xFF;
    uint8_t sg = (start >> 8) & 0xFF;
    uint8_t sb = start & 0xFF;
    
    uint8_t tr = (target >> 16) & 0xFF;
    uint8_t tg = (target >> 8) & 0xFF;
    uint8_t tb = target & 0xFF;
    
    uint8_t r = interpolate(sr, tr, progress);
    uint8_t g = interpolate(sg, tg, progress);
    uint8_t b = interpolate(sb, tb, progress);
    
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
