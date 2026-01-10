#ifdef ESP32
#include <ArduinoOTA.h>
#endif

#include "config.h"
#include "scheduler.h"
#include "transition.h"
#include "webserver.h"

// OTA progress flag
volatile bool otaInProgress = false;
void setupArduinoOTA(const char* hostname) {
#ifdef ESP32
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.onStart([]() {
        otaInProgress = true;
    });
    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
    });
    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
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
