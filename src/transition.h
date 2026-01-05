    #include "debug.h"
    // Allow external force of current brightness for smooth slider
    void forceCurrentBrightness(uint8_t value);
#ifndef TRANSITION_H
#define TRANSITION_H

#include <Arduino.h>
#include "config.h"

class TransitionEngine {
public:
    uint8_t getTargetBrightness() const { return _targetBrightness; }
    TransitionEngine();

    void startTransition(uint8_t targetBrightness, uint32_t duration);
    void startColorTransition(uint32_t targetColor1, uint32_t targetColor2, uint32_t duration);
    void update();

    bool isTransitioning();
    uint8_t getCurrentBrightness();
    uint32_t getCurrentColor1();
    uint32_t getCurrentColor2();

    // Allow external force of current brightness for smooth slider
    void forceCurrentBrightness(uint8_t value);

private:
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
