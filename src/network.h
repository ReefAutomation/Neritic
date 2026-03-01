#ifndef NETWORK_H
#define NETWORK_H

#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <string>

// Setup WiFi (STA+AP) and captive portal DNS task
void networkSetup(Configuration &config);

// Call in main loop - handles periodic STA reconnect
void networkLoop(Configuration &config);

// Returns true if STA interface has an IP
bool networkIsStaConnected();

// Returns true if currently in AP-only or AP fallback mode
bool networkIsApMode();

// Returns current IP address as string (AP or STA)
std::string getCurrentIpString(const Configuration &config);

#endif // NETWORK_H
