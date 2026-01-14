#ifndef STATE_H
#define STATE_H

#include "config.h"

struct SystemState {
    bool power = false;
    uint8_t brightness = 0;
    uint8_t effect = 0;
    EffectParams params;
    uint32_t transitionTime = 5000;
    uint8_t preset = 0;
    bool inTransition = false;
    int8_t prevEffect = -1;
    EffectParams prevParams;
};

extern SystemState state;
void applyPreset(uint8_t presetId);
void setPower(bool power);
void setBrightness(uint8_t brightness);
void setEffect(uint8_t effect, const EffectParams& params);
void setUserColor(const uint32_t* color, size_t count);
void updateLEDs();

#endif // STATE_H
