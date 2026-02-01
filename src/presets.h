#pragma once
#include "config.h"
#include <ArduinoJson.h>
#include <vector>

// Preset management API
bool loadPresets(std::vector<Preset> &presets);
bool savePresets(const std::vector<Preset> &presets);
void resetPresetsFile();
