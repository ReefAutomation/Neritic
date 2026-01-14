#include <Arduino.h>
#include "transition.h"
#include "bus_manager.h"
#include "colors.h"

// Frame blending API
void TransitionEngine::setPreviousFrame(const std::vector<uint32_t>& frame) {
    this->previousFrame = frame;
    debugPrint("[TransitionEngine::setPreviousFrame] previousFrame: ");
    char buf[12];
    for (size_t i = 0; i < previousFrame.size(); ++i) {
        snprintf(buf, sizeof(buf), "#%08X", previousFrame[i]);
        debugPrint(buf); debugPrint(" ");
    }
    debugPrintln("");
}
void TransitionEngine::setTargetFrame(const std::vector<uint32_t>& frame) {
    this->targetFrame = frame;
    debugPrint("[TransitionEngine::setTargetFrame] targetFrame: ");
    char buf[12];
    for (size_t i = 0; i < targetFrame.size(); ++i) {
        snprintf(buf, sizeof(buf), "#%08X", targetFrame[i]);
        debugPrint(buf); debugPrint(" ");
    }
    debugPrintln("");
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
    if (allZero) {
        debugPrintln("[TransitionEngine::getBlendedFrame] WARNING: All blended frame colors are zero!");
        debugPrint("progress: "); debugPrintln(progress, 3);
        debugPrint("brightness: ");
        if (brightnessOnly) {
            uint8_t startBrightness = _startBrightness;
            uint8_t endBrightness = _targetBrightness;
            uint8_t blendedBrightness = (uint8_t)(startBrightness * (1.0f - progress) + endBrightness * progress);
            debugPrintln((int)blendedBrightness);
        } else {
            debugPrintln((int)_targetBrightness);
        }
        debugPrint("previousFrame: ");
        char buf[10];
        for (size_t i = 0; i < previousFrame.size(); ++i) {
            snprintf(buf, sizeof(buf), "#%08X", previousFrame[i]);
            debugPrint(buf); debugPrint(" ");
        }
        debugPrintln("");
        debugPrint("targetFrame: ");
        for (size_t i = 0; i < targetFrame.size(); ++i) {
            snprintf(buf, sizeof(buf), "#%08X", targetFrame[i]);
            debugPrint(buf); debugPrint(" ");
        }
        debugPrintln("");
    }
    return blended;
}
void TransitionEngine::forceCurrentBrightness(uint8_t value) {
    _currentBrightness = value;
    debugPrintln("[Transition] forceCurrentBrightness: " + String(value));
}

void TransitionEngine::forceCurrentColor(uint32_t color1, uint32_t color2) {
    _currentColor1 = color1;
    _currentColor2 = color2;
    debugPrint("[Transition] forceCurrentColor: 1=");
    debugPrintln(String(color1));
    debugPrint("[Transition] forceCurrentColor: 2=");
    debugPrintln(String(color2));
}

TransitionEngine::TransitionEngine() {}

void TransitionEngine::startTransition(uint8_t targetBrightness, uint32_t duration) {
    // Always start a transition, even if brightness does not change, to allow color transitions
    _startBrightness = _currentBrightness;
    _targetBrightness = targetBrightness;
    _startTime = millis();
    _duration = duration < ABSOLUTE_MIN_TRANSITION ? ABSOLUTE_MIN_TRANSITION : duration;
    _active = true;
    debugPrint("[Transition] startTransition: from ");
    debugPrint(String(_startBrightness));
    debugPrint(" to ");
    debugPrint(String(_targetBrightness));
    debugPrint(", duration: ");
    debugPrint(String(_duration));
    debugPrint(", requested: ");
    debugPrintln(String(duration));
}

void TransitionEngine::startColorTransition(uint32_t targetColor1, uint32_t targetColor2, uint32_t duration) {
    _startColor1 = _currentColor1;
    _targetColor1 = targetColor1;
    _startColor2 = _currentColor2;
    _targetColor2 = targetColor2;
    // Do NOT set _active or update timing here; only brightness transition controls timing
    char buf[12];
    debugPrint("[Transition] startColorTransition: from ");
    snprintf(buf, sizeof(buf), "#%08X", _startColor1);
    debugPrint(buf);
    debugPrint(", ");
    snprintf(buf, sizeof(buf), "#%08X", _startColor2);
    debugPrint(buf);
    debugPrint(" to ");
    snprintf(buf, sizeof(buf), "#%08X", _targetColor1);
    debugPrint(buf);
    debugPrint(", ");
    snprintf(buf, sizeof(buf), "#%08X", _targetColor2);
    debugPrint(buf);
    debugPrint(", duration: ");
    debugPrintln(String(duration));
}

void TransitionEngine::update() {
    if (!_active) return;

    uint32_t elapsed = millis() - _startTime;
    if (elapsed >= _duration) {
        // Transition complete
        debugPrintln("[Transition] complete");
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
    uint8_t r, g, b, w;
    blend_rgbw_brightness(start, target, progress, 255, r, g, b, w);
    return pack_rgbw(r, g, b, w);
}
