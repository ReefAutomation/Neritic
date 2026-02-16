#include "network.h"
#include "debug.h"
#include "config.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

static bool apFallbackTriggered = false;

DNSServer captiveDnsServer;

void networkSetup(Configuration& config) {
#ifdef ESP8266
    WiFi.hostname(config.network.hostname);
#else
    WiFi.setHostname(config.network.hostname.c_str());
#endif
    if (config.network.ssid.length() == 0) {
        apFallbackTriggered = true;
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
        startCaptivePortal(WiFi.softAPIP());
        return;
    }
    if (config.network.ssid.length() > 0 && !apFallbackTriggered) {
        const unsigned long wifiTimeout = 10000;
        unsigned long wifiStart = millis();
        bool wrongPassword = false;
        WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && !apFallbackTriggered && (millis() - wifiStart < wifiTimeout)) {
            delay(500);
        #if defined(ESP8266)
            if (WiFi.status() == WL_WRONG_PASSWORD) {
        #else
            if (WiFi.status() == WL_CONNECT_FAILED) {
        #endif
                wrongPassword = true;
                break;
            }
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            stopCaptivePortal();
            return;
        }
        if (apFallbackTriggered || wrongPassword || (millis() - wifiStart >= wifiTimeout)) {
            apFallbackTriggered = true;
        }
    }
    #ifndef ESP8266
    if (WiFi.status() != WL_CONNECTED) {
        apFallbackTriggered = true;
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
        startCaptivePortal(WiFi.softAPIP());
        return;
    }
    #endif
    apFallbackTriggered = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
    startCaptivePortal(WiFi.softAPIP());
}


// Returns the current IP as a string (AP or STA mode)
String getCurrentIpString(const Configuration& config) {
#ifdef ESP8266
    uint8_t mode = WiFi.getMode();
#else    
    wifi_mode_t mode = WiFi.getMode();
#endif    
    if (mode == WIFI_AP || mode == WIFI_AP_STA) {
        return WiFi.softAPIP().toString();
    } else {
        return WiFi.localIP().toString();
    }
}

// Handles WiFi reconnect and captive portal DNS (call in main loop)
void networkLoop(Configuration& config) {
    // WiFi reconnect logic: if not in AP mode and not connected, try to reconnect every 10 seconds
    static uint32_t lastWiFiCheck = 0;
    static int wifiReconnectAttempts = 0;
    const int wifiReconnectInterval = 10000;
    const int maxWiFiReconnectAttempts = 5;
#ifdef ESP8266
    uint8_t mode = WiFi.getMode();
#else    
    wifi_mode_t mode = WiFi.getMode();
#endif    
    if (mode != WIFI_AP && mode != WIFI_AP_STA && config.network.ssid.length() > 0) {
        if (WiFi.status() != WL_CONNECTED) {
            uint32_t now = millis();
            if (now - lastWiFiCheck > wifiReconnectInterval) {
                WiFi.disconnect();
                delay(100);
                WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
                wifiReconnectAttempts++;
                lastWiFiCheck = now;
                if (wifiReconnectAttempts >= maxWiFiReconnectAttempts) {
                    apFallbackTriggered = true;
                    WiFi.mode(WIFI_AP_STA);
                    WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
                    startCaptivePortal(WiFi.softAPIP());
                    wifiReconnectAttempts = 0;
                }
            }
        } else {
            wifiReconnectAttempts = 0;
        }
    }
#ifdef ESP8266
    uint8_t modeNow = WiFi.getMode();
#else
    wifi_mode_t modeNow = WiFi.getMode();
#endif
    if (modeNow == WIFI_AP || modeNow == WIFI_AP_STA) {
        handleCaptivePortalDns();
    }
}


String urlDecode(const String &input) {
    String decoded;
    char temp[3] = {0};
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '%') {
            if (i + 2 < input.length()) {
                temp[0] = input[i + 1];
                temp[1] = input[i + 2];
                decoded += (char)strtol(temp, nullptr, 16);
                i += 2;
            }
        } else if (input[i] == '+') {
            decoded += ' ';
        } else {
            decoded += input[i];
        }
    }
    return decoded;
}

void setupWiFi(Configuration& config) {
    debugPrintln("[WiFi] setupWiFi() called");
    debugPrint("Connecting to WiFi");
#ifdef ESP8266
    WiFi.hostname(config.network.hostname);
#else
    WiFi.setHostname(config.network.hostname.c_str());
#endif
    if (config.network.ssid.length() > 0 && !apFallbackTriggered) {
        debugPrintln("");
        debugPrintln("[WiFi] Calling WiFi.begin");
        const unsigned long wifiTimeout = 10000;
        unsigned long wifiStart = millis();
        bool wrongPassword = false;
        WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && !apFallbackTriggered && (millis() - wifiStart < wifiTimeout)) {
            delay(500);
            debugPrint(".");
#ifdef ESP8266
            debugPrint(", error WL_WRONG_PASSWORD = ");
            debugPrintln(String(WL_WRONG_PASSWORD).c_str());
            if (WiFi.status() == WL_WRONG_PASSWORD) {
                debugPrintln("\n[WiFi] Wrong password detected, switching to AP mode");
                wrongPassword = true;
                break;
            }
#else
            debugPrint(", error WL_CONNECT_FAILED = ");
            debugPrint(String(WL_CONNECT_FAILED).c_str());
#if defined(ARDUINO_ARCH_ESP32)
            debugPrint(", esp_err_t = ");
            debugPrintln(String(WiFi.status()).c_str());
#endif
            if (WiFi.status() == WL_CONNECT_FAILED) {
                debugPrintln("\n[WiFi] Connection failed, switching to AP mode");
                wrongPassword = true;
                break;
            }
#endif
            attempts++;
        }
        debugPrintln("");
        debugPrintln("[WiFi] Connection attempt done");
        if (WiFi.status() == WL_CONNECTED) {
            debugPrintln("");
            debugPrintln("[WiFi] Connected!");
            debugPrint("Connected! IP: ");
            debugPrintln(WiFi.localIP());
            stopCaptivePortal();
            return;
        }
        if (apFallbackTriggered || wrongPassword || (millis() - wifiStart >= wifiTimeout)) {
            debugPrintln("\n[WiFi] Fallback to AP mode after failed attempts or timeout");
            apFallbackTriggered = true;
        }
    }
#ifndef ESP8266
    if (WiFi.status() != WL_CONNECTED) {
        debugPrintln("");
        debugPrintln("[WiFi] Not connected after attempts, switching to AP mode");
        apFallbackTriggered = true;
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
        debugPrint("AP IP: ");
        debugPrintln(WiFi.softAPIP());
        startCaptivePortal(WiFi.softAPIP());
        return;
    }
#endif
    debugPrintln("");
    debugPrintln("[WiFi] Starting Access Point mode");
    apFallbackTriggered = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
    debugPrint("AP IP: ");
    debugPrintln(WiFi.softAPIP());
    startCaptivePortal(WiFi.softAPIP());
}

void handleWiFiReconnect(Configuration& config) {
    static uint32_t lastWiFiCheck = 0;
    static int wifiReconnectAttempts = 0;
    const int wifiReconnectInterval = 10000;
    const int maxWiFiReconnectAttempts = 5;
    if (WiFi.getMode() != WIFI_AP && config.network.ssid.length() > 0) {
        if (WiFi.status() != WL_CONNECTED) {
            uint32_t now = millis();
            if (now - lastWiFiCheck > wifiReconnectInterval) {
                debugPrintln("[WiFi] Lost connection, attempting reconnect...");
                WiFi.disconnect();
                delay(100);
                WiFi.begin(config.network.ssid.c_str(), config.network.password.c_str());
                wifiReconnectAttempts++;
                lastWiFiCheck = now;
                if (wifiReconnectAttempts >= maxWiFiReconnectAttempts) {
                    debugPrintln("[WiFi] Too many failed reconnects, switching to AP mode");
                    apFallbackTriggered = true;
                    WiFi.mode(WIFI_AP_STA);
                    WiFi.softAP(config.network.hostname.c_str(), config.network.apPassword.c_str());
                    startCaptivePortal(WiFi.softAPIP());
                    wifiReconnectAttempts = 0;
                }
            }
        } else {
            wifiReconnectAttempts = 0;
        }
    }
}

void startCaptivePortal(const IPAddress &apIP) {
    captiveDnsServer.start(53, "*", apIP);
}

void stopCaptivePortal() {
    captiveDnsServer.stop();
}

void handleCaptivePortalDns() {
    captiveDnsServer.processNextRequest();
}
