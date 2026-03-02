#ifndef NATIVE_HOMEKIT_BACKEND_H
#define NATIVE_HOMEKIT_BACKEND_H

#include <cstdint>

typedef bool (*NativeHomeKitAccessoryPowerCallback)(bool power);
typedef bool (*NativeHomeKitAccessoryBrightnessCallback)(uint8_t brightnessPercent);

bool nativeHomeKitBackendInit(const char *accessoryName, const char *setupCode,
                              const char *setupId);
void nativeHomeKitBackendLoop();
void nativeHomeKitBackendSetState(bool power, uint8_t brightnessPercent);
void nativeHomeKitBackendRegisterAccessoryCallbacks(
    NativeHomeKitAccessoryPowerCallback powerCallback,
    NativeHomeKitAccessoryBrightnessCallback brightnessCallback);
const char *nativeHomeKitBackendStatus();

#endif // NATIVE_HOMEKIT_BACKEND_H
