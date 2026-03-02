#ifndef HAP_ENGINE_H
#define HAP_ENGINE_H

#include <cstdint>

typedef bool (*HapEnginePowerWriteCallback)(bool power);
typedef bool (*HapEngineBrightnessWriteCallback)(uint8_t brightnessPercent);

struct HapEngineConfig {
  const char *accessoryName;
  const char *setupCode;
  const char *setupId;
};

bool hapEngineInit(const HapEngineConfig &config,
                   HapEnginePowerWriteCallback powerWriteCallback,
                   HapEngineBrightnessWriteCallback brightnessWriteCallback);
void hapEngineLoop();
void hapEngineNotifyState(bool power, uint8_t brightnessPercent);
const char *hapEngineStatus();

#endif // HAP_ENGINE_H
