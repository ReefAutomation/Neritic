#ifndef NATIVE_HOMEKIT_H
#define NATIVE_HOMEKIT_H

#include "config.h"
#include "state.h"
#include <string>

typedef void (*NativeHomeKitBackendStateCallback)(bool power,
												  uint8_t brightnessPercent);

bool nativeHomeKitCompiled();
bool nativeHomeKitModeEnabled(const Configuration &config);
void nativeHomeKitSetup(const Configuration &config);
void nativeHomeKitLoop();
void nativeHomeKitSync(const SystemState &state);
std::string nativeHomeKitStatusString(const Configuration &config);

void nativeHomeKitSetBackendStateCallback(
	NativeHomeKitBackendStateCallback callback);
bool nativeHomeKitApplyPowerFromAccessory(bool power);
bool nativeHomeKitApplyBrightnessFromAccessory(uint8_t brightnessPercent);

#endif // NATIVE_HOMEKIT_H
