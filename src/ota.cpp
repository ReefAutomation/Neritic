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
        debugPrintln("[OTA] Start");
    });
    ArduinoOTA.onEnd([]() {
        debugPrintln("[OTA] End");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        debugPrint("[OTA] Error: ");
        debugPrintln((int)error);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        debugPrint("[OTA] Progress: ");
        debugPrint((progress * 100) / total);
        debugPrintln("%");
    });
    ArduinoOTA.begin();
    debugPrint("[OTA] Hostname: ");
    debugPrintln(hostname);
#endif
}

void handleArduinoOTA() {
#ifdef ESP32
    ArduinoOTA.handle();
#endif
}
