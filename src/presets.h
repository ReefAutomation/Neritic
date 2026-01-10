#pragma once
#include <vector>
#include <ArduinoJson.h>
#include "config.h"

// Preset management API
bool loadPresets(std::vector<Preset>& presets);
bool savePresets(const std::vector<Preset>& presets);
void resetPresetsFile();
