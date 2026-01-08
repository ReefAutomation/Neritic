#ifdef ESP32
#include <ArduinoOTA.h>
#endif

#include "config.h"
#include "scheduler.h"
#include "transition.h"
#include "webserver.h"

void setupArduinoOTA(const char* hostname) {
#ifdef ESP32
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.onStart([]() {
    });
    ArduinoOTA.onEnd([]() {
    });
    ArduinoOTA.onError([](ota_error_t error) {
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    });
    ArduinoOTA.begin();
#endif
}

void handleArduinoOTA() {
#ifdef ESP32
    ArduinoOTA.handle();
#endif
}
