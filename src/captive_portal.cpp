#include "webserver.h"
#include "captive_portal.h"
#ifdef ESP8266
#include <DNSServer.h>
#else
#include <DNSServer.h>
#endif

// Captive portal DNS server instance
DNSServer captiveDnsServer;

void startCaptivePortal(const IPAddress& apIP) {
    // Start DNS server to redirect all domains to AP IP
    captiveDnsServer.start(53, "*", apIP);
}

void stopCaptivePortal() {
    captiveDnsServer.stop();
}

void handleCaptivePortalDns() {
    captiveDnsServer.processNextRequest();
}
