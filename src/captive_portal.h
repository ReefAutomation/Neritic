#pragma once
#ifdef ESP8266
#include <DNSServer.h>
#else
#include <DNSServer.h>
#endif
#include <Arduino.h>

void startCaptivePortal(const IPAddress& apIP);
void stopCaptivePortal();
void handleCaptivePortalDns();
