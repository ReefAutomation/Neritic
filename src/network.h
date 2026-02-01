
#ifndef NETWORK_H
#define NETWORK_H

// Core includes
#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "config.h"
#include "debug.h"

// --- Network API ---

// Setup WiFi and captive portal
void networkSetup(Configuration& config);

// Main loop handler for network/captive portal
void networkLoop(Configuration& config);
// Processes captive portal DNS in AP or AP+STA mode
void processCaptivePortalDNS();

// Get current IP as string (AP or STA)
String getCurrentIpString(const Configuration& config);

// Setup web server WiFi/captive portal handlers
void setupWiFiHandlers(AsyncWebServer* server, Configuration* config);


// Captive portal DNS control
void startCaptivePortal(const IPAddress &apIP);
void stopCaptivePortal();
void handleCaptivePortalDns();

#endif // NETWORK_H
