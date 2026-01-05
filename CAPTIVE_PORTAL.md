# Captive Portal for Aquarium LED Controller

This file describes the captive portal feature for WiFi configuration in AP mode.

## Feature Overview
When the device starts in Access Point (AP) mode (because it cannot connect to a saved WiFi network), it will automatically serve a captive portal page. This page will prompt the user to enter their WiFi SSID and password. Once submitted, the device will attempt to connect to the provided WiFi credentials and, if successful, save them for future use.

## Implementation Plan
1. **DNS Redirect**: Start a DNS server that redirects all requests to the device's IP (captive portal behavior).
2. **Portal Webpage**: Serve a simple HTML form at `/` for SSID and password input.
3. **Handle Submission**: On form submission, save credentials, attempt WiFi connection, and reboot if successful.
4. **Fallback**: If connection fails, return to AP mode and show the portal again.

## Libraries
- ESP8266: Use `DNSServer` and `ESPAsyncWebServer`.
- ESP32: Use `DNSServer` and `ESPAsyncWebServer`.

## Files to Update
- `src/main.cpp`: Add captive portal logic to AP mode.
- `src/webserver.cpp`/`.h`: Add route for WiFi config page and handler for POST.
- Add DNS server startup in AP mode.

## User Experience
- When connecting to the device's AP, any web address will redirect to the portal.
- User enters WiFi credentials and submits.
- Device attempts to connect, then reboots or shows error.

---
This feature ensures easy WiFi setup for end users.
