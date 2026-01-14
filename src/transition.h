#ifndef TRANSITION_H
#define TRANSITION_H

#include <Arduino.h>
#include "config.h"
#include "debug.h"

class TransitionEngine {
public:
    // Setters for transition start values (for smooth interruption)
    void setStartBrightness(uint8_t value) { _startBrightness = value; }
    void setStartColor1(uint32_t value) { _startColor1 = value; }
    void setStartColor2(uint32_t value) { _startColor2 = value; }
    const std::vector<uint32_t>& getTargetFrame() const { return targetFrame; }
    const std::vector<uint32_t>& getPreviousFrame() const { return previousFrame; }
    // Frame blending API
    void setPreviousFrame(const std::vector<uint32_t>& frame);
    void setTargetFrame(const std::vector<uint32_t>& frame);
    void clearFrames();
    std::vector<uint32_t> getBlendedFrame(float progress, bool brightnessOnly);
    uint8_t getStartBrightness() const { return _startBrightness; }
    uint8_t getTargetBrightness() const { return _targetBrightness; }
    TransitionEngine();

    // Getters for transition timing
    uint32_t getStartTime() const { return _startTime; }
    uint32_t getDuration() const { return _duration; }
    // Allow external force of current brightness for smooth slider
    void forceCurrentBrightness(uint8_t value);
    // Allow external force of current color for instant color set
    void forceCurrentColor(uint32_t color1, uint32_t color2);
    void startTransition(uint8_t targetBrightness, uint32_t duration);
    void startColorTransition(uint32_t targetColor1, uint32_t targetColor2, uint32_t duration);
    void update();

    bool isTransitioning();
    uint8_t getCurrentBrightness();
    uint32_t getCurrentColor1();
    uint32_t getCurrentColor2();

private:
    std::vector<uint32_t> previousFrame;
    std::vector<uint32_t> targetFrame;
    bool _active = false;
    uint32_t _startTime = 0;
    uint32_t _duration = 0;

    // Brightness transition
    uint8_t _startBrightness = 0;
    uint8_t _targetBrightness = 0;
    uint8_t _currentBrightness = 0;

    // Color transitions
    uint32_t _startColor1 = 0;
    uint32_t _targetColor1 = 0;
    uint32_t _currentColor1 = 0;

    uint32_t _startColor2 = 0;
    uint32_t _targetColor2 = 0;
    uint32_t _currentColor2 = 0;

    uint8_t interpolate(uint8_t start, uint8_t target, float progress);
    uint32_t interpolateColor(uint32_t start, uint32_t target, float progress);
};

#endif
