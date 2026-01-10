#pragma once

extern volatile bool otaInProgress;
void setupArduinoOTA(const char* hostname);
void handleArduinoOTA();
