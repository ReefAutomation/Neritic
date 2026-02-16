// =================== INCLUDES & GLOBALS ===================
#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR)
#include <pgmspace.h>
#endif
#include "inc/index_html.inc"
// #include "inc/app_js.inc" // migrated to SPA, now included in index_js.inc
// #include "inc/config_js.inc" // migrated to SPA, now included in index_js.inc
#include "inc/style_css.inc"
#include "network.h"
#include "inc/index_js.inc"

#include <LittleFS.h>
#include <Ticker.h>
#include <WiFiClientSecure.h>

#if defined(ESP32)
#include <Update.h>
#elif defined(ESP8266)
#include <Updater.h>
#endif

#include "effects.h"
#include "ota.h"
#include "presets.h"
#include "state.h"
#include "transition.h"
#include "inc/version.inc"
#include "webserver.h"


// Global externs for transition state
extern TransitionEngine transition;
extern SystemState state;
extern TransitionEngine::PendingTransitionState pendingTransition;

#include <set>
// Extern for OTA ACK handshake flag
extern volatile bool otaAckReceived;
// Track if we've received a message from each client (for ws handshake logic)
#include <map>
static std::map<uint32_t, bool> wsClientReceivedFirstMsg;
// Track WebSocket client IDs subscribed to OTA status
static std::set<uint32_t> otaSubscribedClients;
// Track clients that will become OTA-subscribed on next connect
static std::set<uint32_t> pendingOtaClients;

// Interval for live LED data broadcast (ms)
#define LIVE_LED_BROADCAST_INTERVAL_MS 400

static Ticker liveLedTicker;
static bool liveLedTick = false;
extern volatile bool otaInProgress;

// CORS headers for API responses
static const char *CORS_HEADERS[][2] = {
    {"Access-Control-Allow-Origin", "*"},
    {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
    {"Access-Control-Allow-Headers", "Content-Type"}};
static const size_t CORS_HEADER_COUNT =
    sizeof(CORS_HEADERS) / sizeof(CORS_HEADERS[0]);

// Cached effect list JSON
static String cachedEffectsJson;
static bool effectsCacheReady = false;

// =================== STUBS & HELPERS ===================
void pauseEffects() {}
void pauseWebServer() {}
void resumeEffects() {}
void resumeWebServer() {}
static String urlDecode(const String &input);
static String extractJsonBody(AsyncWebServerRequest *request, uint8_t *data,
                              size_t len);
static String extractPostBody(AsyncWebServerRequest *request);
template <typename TDoc>
static bool parseJsonOrRespond(AsyncWebServerRequest *request,
                               const String &jsonStr, TDoc &doc);
void buildEffectsCache();

// =================== WEBSERVERMANAGER METHODS ===================

// --- OTA Status WebSocket Broadcast ---
void WebServerManager::broadcastOtaStatus(const String &status,
                                          const String &message, int progress) {
  StaticJsonDocument<256> doc;
  doc["type"] = "ota_status";
  doc["status"] = status;
  if (message.length() > 0)
    doc["message"] = message;
  if (progress >= 0)
    doc["progress"] = progress;
  String json;
  serializeJson(doc, json);
  if (_ws) {
    // Only send to OTA-subscribed clients
    for (auto &client : _ws->getClients()) {
      if (otaSubscribedClients.count(client.id())) {
        client.text(json);
      }
    }
  }
}

// Helper: Extract JSON body from POST request (for upload handler)
static String extractJsonBody(AsyncWebServerRequest *request, uint8_t *data,
                              size_t len) {
  String jsonStr;
  if (len > 0 && data != nullptr) {
    for (size_t i = 0; i < len; ++i)
      jsonStr += (char)data[i];
  } else {
    jsonStr = request->arg("plain");
  }
  return jsonStr;
}

// Helper: Extract POST body for main POST handler
static String extractPostBody(AsyncWebServerRequest *request) {
  String body = request->arg("plain");
  if (body.length() == 0 && request->params() > 0) {
    body = request->getParam((size_t)0)->value();
  }
  if (body.length() == 0) {
    if (request->_tempObject &&
        ((String *)request->_tempObject)->length() > 0) {
      body = *((String *)request->_tempObject);
    }
  }
  return body;
}

// Helper: Deserialize JSON and handle error response
template <typename TDoc>
static bool parseJsonOrRespond(AsyncWebServerRequest *request,
                               const String &jsonStr, TDoc &doc) {
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    AsyncWebServerResponse *resp = request->beginResponse(
        400, "application/json", "{\"error\":\"Invalid JSON\"}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
    return false;
  }
  return true;
}

void buildEffectsCache() {
  // Populate effects array from the effect registry (portable vector-based)
  StaticJsonDocument<4096> doc;
  JsonArray effects = doc.createNestedArray("effects");
  for (size_t i = 0; i < effectRegistry.size(); ++i) {
    JsonObject eff = effects.createNestedObject();
    eff["id"] = effectRegistry[i].id;
    eff["name"] = effectRegistry[i].name;
  }
  cachedEffectsJson.clear();
  serializeJson(doc, cachedEffectsJson);
  effectsCacheReady = true;
}

static String urlDecode(const String &input) {
  String decoded;
  char temp[3] = {0};
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      if (i + 2 < input.length()) {
        temp[0] = input[i + 1];
        temp[1] = input[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

WebServerManager::WebServerManager(Configuration *config,
                                   Scheduler *scheduler) {
  _config = config;
  _scheduler = scheduler;
  _server = new AsyncWebServer(80);
  _ws = new AsyncWebSocket("/ws");
}

void WebServerManager::begin() {
  setupWebSocket();
  setupRoutes();
  buildEffectsCache();
  _server->begin();
  // Start ticker for live LED broadcast
  liveLedTicker.attach_ms(LIVE_LED_BROADCAST_INTERVAL_MS,
                          []() { liveLedTick = true; });
}
void WebServerManager::update() {
  _ws->cleanupClients();

  // --- Periodic live LED data broadcast as binary blob ---
  if (!liveLedTick) return;
  liveLedTick = false;

  // Suppress live LED broadcast if OTA is in progress
  if (otaInProgress) return;

  uint16_t n = _config->led.count;
  size_t wsCount = _ws->count();

  // No clients or no LEDs, nothing to broadcast
  if (wsCount == 0 || n == 0) return;

  // If all clients are OTA-subscribed, skip live LED broadcast entirely
  if (otaSubscribedClients.size() == wsCount) return;

  // Only process clients that have completed handshake and are not OTA-subscribed
  bool allReady = true;
  for (auto &c : _ws->getClients()) {
    if (!wsClientReceivedFirstMsg.count(c.id()) || !wsClientReceivedFirstMsg[c.id()]) {
      continue; // Not handshaked, skip
    }
    if (!otaSubscribedClients.count(c.id()) && c.queueIsFull()) {
      allReady = false;
      break;
    }
  }
  if (!allReady) {
    static uint32_t lastSkipLog = 0;
    uint32_t now = millis();
    if (now - lastSkipLog > 1000) {
      lastSkipLog = now;
      debugPrintln("[WS] Skipped live frame: client queue full");
    }
    return;
  }
  // Prepare LED frame buffer
  const std::vector<uint32_t> *src =
      (g_outputFramePtr && g_outputFramePtr->size() >= n)
          ? g_outputFramePtr
          : nullptr;
  std::vector<uint8_t> buf(n * 4, 0);
  if (src) {
    for (uint16_t i = 0; i < n; ++i) {
      uint32_t c = (i < src->size()) ? (*src)[i] : 0;
      buf[i * 4 + 0] = (c >> 24) & 0xFF;
      buf[i * 4 + 1] = (c >> 16) & 0xFF;
      buf[i * 4 + 2] = (c >> 8) & 0xFF;
      buf[i * 4 + 3] = c & 0xFF;
    }
  }
  for (auto &c : _ws->getClients()) {
    // Only send to clients that have handshaked and are not OTA-subscribed
    if (!wsClientReceivedFirstMsg.count(c.id()) || !wsClientReceivedFirstMsg[c.id()]) continue;
    if (otaSubscribedClients.count(c.id())) continue;
    c.binary(buf.data(), buf.size());
  }
}

void WebServerManager::setupWebSocket() {
  _ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      client->setCloseClientOnQueueFull(false);
      // If pending OTA, move to real OTA set
      if (pendingOtaClients.count(client->id())) {
        otaSubscribedClients.insert(client->id());
        pendingOtaClients.erase(client->id());
      } else {
        otaSubscribedClients.erase(client->id());
      }
      wsClientReceivedFirstMsg[client->id()] = false;
    } else if (type == WS_EVT_DISCONNECT) {
      // Remove from OTA set and tracking on disconnect
      otaSubscribedClients.erase(client->id());
      wsClientReceivedFirstMsg.erase(client->id());
    } else if (type == WS_EVT_ERROR) {
      debugPrintln(String("[WS] ERROR: client ") + client->id());
    } else if (type == WS_EVT_DATA) {
      auto *info = (AwsFrameInfo *)arg;
      if (info && info->opcode == WS_TEXT && data && len > 0) {
        #if defined(ESP8266)
        String msg;
        for (size_t i = 0; i < len; ++i) msg += (char)data[i];
        #else
        String msg((const char *)data, len);
        #endif
        if (!wsClientReceivedFirstMsg[client->id()]) {
          // First message from client: handshake
          if (msg.indexOf("\"type\":\"ota_client\"") != -1) {
            otaSubscribedClients.insert(client->id());
            // Send state for OTA client so SPA gets real-time info (clock, etc)
            client->text(getStateJSON());
          } else {
            // Normal client: send state
            otaSubscribedClients.erase(client->id());
            client->text(getStateJSON());
          }
          wsClientReceivedFirstMsg[client->id()] = true;
        } else {
          // Subsequent messages
          if (msg.indexOf("\"type\":\"ping\"") != -1) {
            // Optionally respond with pong or just ignore
          } else if (msg.indexOf("\"type\":\"ota_client\"") != -1) {
            otaSubscribedClients.insert(client->id());
          } else if (msg.indexOf("\"type\":\"state\"") != -1) {
            otaSubscribedClients.erase(client->id());
            client->text(getStateJSON());
          } else if (msg.indexOf("\"type\":\"ota_ack\"") != -1) {
            // OTA ACK received from client
            otaAckReceived = true;
          }
        }
      }
    }
  });
  _server->addHandler(_ws);
}

void WebServerManager::setupRoutes() {

  // Debug: Log every incoming HTTP request
  _server->onNotFound([this](AsyncWebServerRequest *request) {
    debugPrintln(String("[HTTP] NotFound: ") + request->url());
    request->send(404, "text/plain", "Not Found");
  });

  // Helper: Log all requests (valid or not)
  auto logRequest = [](AsyncWebServerRequest *request) {
    String logMsg = String("[HTTP] ") +
                    (request->method() == HTTP_GET    ? "GET "
                     : request->method() == HTTP_POST ? "POST "
                                                      : "OTHER ") +
                    request->url();
    debugPrintln(logMsg);
  };
  // Version API endpoint
  _server->on("/api/version", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(
                    200, "application/json",
                    String("{\"version\":\"") + FW_VERSION + "\"}");
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  // Update API endpoint: GET for remote manifest, POST for OTA update
  _server->on("/api/update", HTTP_ANY,
    [logRequest, this](AsyncWebServerRequest *request) {
      logRequest(request);
      if (request->method() == HTTP_GET) {
        // Serve remote manifest (firmware version and metadata)
        String manifestJson = fetchRemoteManifestJson();
        if (manifestJson.length() == 0) {
          AsyncWebServerResponse *resp = request->beginResponse(404, "application/json", "{\"error\":\"No release manifest found\"}");
          for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
            resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
          resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate, max-age=0");
          resp->addHeader("Pragma", "no-cache");
          request->send(resp);
          return;
        }
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", manifestJson);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
          resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate, max-age=0");
        resp->addHeader("Pragma", "no-cache");
        request->send(resp);
        return;
      }
      if (request->method() == HTTP_POST) {
        // Find the WebSocket client with the same IP as the HTTP request
        IPAddress reqIp = request->client()->remoteIP();
        for (auto &client : _ws->getClients()) {
          if (client.remoteIP() == reqIp) {
            pendingOtaClients.insert(client.id());
          }
        }
#if defined(ESP32)
        xTaskCreatePinnedToCore(otaTask, "otaTask", 16384, nullptr, 1, nullptr, 1);
#endif
        AsyncWebServerResponse *resp = request->beginResponse(
            200, "application/json",
            "{\"success\":true,\"message\":\"OTA update started in background. Device will update and reboot.\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
          resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        return;
      }
      // Method not allowed
      AsyncWebServerResponse *resp = request->beginResponse(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
    });

  // Update API endpoint: POST /api/update (OTA with .bin.gz support)
  _server->on("/api/update", HTTP_POST,
    [logRequest, this](AsyncWebServerRequest *request) {
      logRequest(request);
      // Find the WebSocket client with the same IP as the HTTP request
      IPAddress reqIp = request->client()->remoteIP();
      for (auto &client : _ws->getClients()) {
        if (client.remoteIP() == reqIp) {
          pendingOtaClients.insert(client.id());
        }
      }
#if defined(ESP32)
      xTaskCreatePinnedToCore(otaTask, "otaTask", 16384, nullptr, 1, nullptr, 1);
#endif
      AsyncWebServerResponse *resp = request->beginResponse(
          200, "application/json",
          "{\"success\":true,\"message\":\"OTA update started in background. Device will update and reboot.\"}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
    });

  // System command API (reboot, update, etc.)
  _server->on("/api/command", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on(
      "/api/command", HTTP_POST,
      [logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(
            200, "application/json",
            "{\"success\":true,\"message\":\"Rebooting\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
          resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        request->onDisconnect([]() {
          delay(100);
          ESP.restart();
        });
      },
      NULL,
      [logRequest](AsyncWebServerRequest *request, uint8_t *data, size_t len,
                   size_t, size_t) {
        logRequest(request);
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, data, len);
        String respJson;
        int status = 200;
        if (error || !doc.containsKey("command")) {
          respJson = "{\"success\":false,\"error\":\"Invalid JSON or missing "
                     "command\"}";
        } else {
          String cmd = doc["command"].as<String>();
          if (cmd == "reboot") {
            respJson = "{\"success\":true,\"message\":\"Rebooting\"}";
          } else if (cmd == "update") {
            respJson = "{\"success\":true,\"message\":\"Update started\"}";
          } else {
            respJson = "{\"success\":false,\"error\":\"Unknown command\"}";
          }
        }
        AsyncWebServerResponse *resp =
            request->beginResponse(status, "application/json", respJson);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
          resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        // Actually perform the command after response
        if (!error && doc.containsKey("command")) {
          String cmd = doc["command"].as<String>();
          if (cmd == "reboot") {
            request->onDisconnect([]() {
              delay(100);
              ESP.restart();
            });
          }
        }
      });
  // CORS preflight for OTA
  _server->on("/ota", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  // OTA Update endpoint (POST /ota, direct binary upload)
  _server->on(
      "/ota", HTTP_POST,
      [logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        AsyncWebServerResponse *resp = nullptr;
        if (Update.hasError()) {
          resp = request->beginResponse(500, "application/json",
                                        "{\"error\":\"OTA Update Failed\"}");
        } else {
          resp = request->beginResponse(
              200, "application/json",
              "{\"success\":true,\"message\":\"Rebooting\"}");
        }
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
          resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        if (!Update.hasError()) {
          request->onDisconnect([]() {
            delay(100);
            ESP.restart();
          });
        }
      },
      NULL,
      [logRequest](AsyncWebServerRequest *request, unsigned char *data,
                   unsigned int len, unsigned int index, unsigned int total) {
        handleOTAUpdate(request, reinterpret_cast<uint8_t *>(data),
                        static_cast<size_t>(len), static_cast<size_t>(index),
                        static_cast<size_t>(total));
      });

  // Captive portal triggers for auto-popup on phones/laptops
  _server->on("/generate_204", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->redirect("/wifi");
              });
  _server->on("/hotspot-detect.html", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->redirect("/wifi");
              });
  _server->on("/ncsi.txt", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->redirect("/wifi");
              });
  _server->on("/connecttest.txt", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->redirect("/wifi");
              });
  // Extra captive portal triggers for maximum compatibility
  _server->on("/favicon.ico", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->send(204); // No Content
              });
  _server->on("/wpad.dat", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->send(204); // No Content
              });

  // Serve web assets from filesystem image
  // Serve static SPA assets
  _server->on("/index.js", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
    logRequest(request);
    request->send_P(200, "application/javascript", web_index_js, web_index_js_len);
  });
  _server->on("/style.css", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
    logRequest(request);
    request->send_P(200, "text/css", web_style_css, web_style_css_len);
  });
  _server->on("/", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
    logRequest(request);
    request->send_P(200, "text/html", web_index_html, web_index_html_len);
  });
  _server->on(
      "/index.html", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
      });
  // Register WiFi endpoints directly
  // WiFi scan endpoint (returns SSID list as JSON, triggers scan)
  _server->on("/wifi/scan", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
    logRequest(request);
    int scanStatus = WiFi.scanComplete();
    if (scanStatus == -2) { // No scan started
      WiFi.scanNetworks(true);
      request->send(200, "application/json", "{\"status\":\"scanning\"}");
    } else if (scanStatus == -1) { // Scan ongoing
      request->send(200, "application/json", "{\"status\":\"scanning\"}");
    } else if (scanStatus >= 0) { // Scan complete
      String json = "[";
      for (int i = 0; i < scanStatus; ++i) {
        if (i > 0) json += ",";
        json += "\"" + WiFi.SSID(i) + "\"";
      }
      json += "]";
      WiFi.scanDelete();
      request->send(200, "application/json", json);
    } else {
      request->send(500, "application/json", "{\"error\":\"scan error\"}");
    }
  });
  // POST handler for /wifi
  _server->on("/wifi", HTTP_POST,
    [this, logRequest](AsyncWebServerRequest *request) {
      logRequest(request);
      if (request->hasParam("ssid", true)) {
        String ssid = urlDecode(request->getParam("ssid", true)->value());
        String password = request->hasParam("password", true)
          ? urlDecode(request->getParam("password", true)->value())
          : "";
        if (ssid.length() > 0) {
          _config->network.ssid = ssid;
          _config->network.password = password;
          _config->save();
          String html = "<html><body><h2>Connecting to WiFi...</h2><p>Device will reboot if successful.</p></body></html>";
          request->send(200, "text/html", html);
          delay(1000);
          ESP.restart();
          return;
        }
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
      }
    },
    nullptr,
    [this, logRequest](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
      logRequest(request);
      String body;
      for (size_t i = 0; i < len; ++i)
        body += (char)data[i];
      String ssid, password;
      int ssidIdx = body.indexOf("ssid=");
      int passIdx = body.indexOf("password=");
      if (ssidIdx != -1) {
        int amp = body.indexOf('&', ssidIdx);
        ssid = urlDecode(body.substring(ssidIdx + 5, amp == -1 ? body.length() : amp));
      }
      if (passIdx != -1) {
        int amp = body.indexOf('&', passIdx);
        password = urlDecode(body.substring(passIdx + 9, amp == -1 ? body.length() : amp));
      }
      if (ssid.length() > 0) {
        _config->network.ssid = ssid;
        _config->network.password = password;
        _config->save();
        String html = "<html><body><h2>Connecting to WiFi...</h2><p>Device will reboot if successful.</p></body></html>";
        request->send(200, "text/html", html);
        delay(1000);
        ESP.restart();
        return;
      }
      request->send_P(200, "text/html", web_index_html, web_index_html_len);
    }
  );

  // GET handler for /wifi
  _server->on("/wifi", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
    logRequest(request);
    request->send_P(200, "text/html", web_index_html, web_index_html_len);
  });
  _server->on("/app.js", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->send_P(200, "application/javascript", web_index_js,
                                web_index_js_len);
              });

  _server->on("/index.js", HTTP_GET,
    [logRequest](AsyncWebServerRequest *request) {
      logRequest(request);
      request->send_P(200, "application/javascript", web_index_js, web_index_js_len);
    });
  _server->on("/index.js", HTTP_GET,
    [logRequest](AsyncWebServerRequest *request) {
      logRequest(request);
      request->send_P(200, "application/javascript", web_index_js, web_index_js_len);
    });
  _server->on(
      "/config.html", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
      });
  _server->on("/config.js", HTTP_GET,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                request->send_P(200, "application/javascript", web_index_js,
                                web_index_js_len);
              });
  _server->on("/style.css", HTTP_GET,
    [logRequest](AsyncWebServerRequest *request) {
      logRequest(request);
      request->send_P(200, "text/css", web_style_css, web_style_css_len);
    });
  _server->on("/style.css", HTTP_GET,
    [logRequest](AsyncWebServerRequest *request) {
      logRequest(request);
      request->send_P(200, "text/css", web_style_css, web_style_css_len);
    });

  // State API
  _server->on("/api/state", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on("/api/state", HTTP_GET,
              [this, logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                handleGetState(request);
              });

  // Main POST handler
  _server->on(
      "/api/state", HTTP_POST,
      [this, logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        String body = extractPostBody(request);
        if (body.length() == 0)
          return;
        handleSetState(request, (uint8_t *)body.c_str(), body.length());
      },
      nullptr,
      // Upload handler for application/json
      [this, logRequest](AsyncWebServerRequest *request, uint8_t *data,
                         size_t len, size_t, size_t) {
        logRequest(request);
        String body = extractJsonBody(request, data, len);
        handleSetState(request, (uint8_t *)body.c_str(), body.length());
      });

  // Effects API: serve cached JSON for all available predefined effect names
  // and indices
  _server->on(
      "/api/effects", HTTP_GET, [logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        if (!effectsCacheReady)
          buildEffectsCache();
        AsyncWebServerResponse *resp =
            request->beginResponse(200, "application/json", cachedEffectsJson);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
          resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
      });

  // Presets API
  _server->on("/api/presets", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on("/api/presets", HTTP_GET,
              [this, logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                handleGetPresets(request);
              });

  _server->on("/api/preset", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on(
      "/api/preset", HTTP_POST,
      [this, logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        String body = extractPostBody(request);
        if (body.length() == 0)
          return; // Prevent double response if upload handler already processed
        handleSetPreset(request, (uint8_t *)body.c_str(), body.length());
      },
      nullptr,
      // Upload handler for application/json
      [this, logRequest](AsyncWebServerRequest *request, uint8_t *data,
                         size_t len, size_t, size_t) {
        logRequest(request);
        String body = extractJsonBody(request, data, len);
        handleSetPreset(request, (uint8_t *)body.c_str(), body.length());
      });

  // Configuration API
  _server->on("/api/config", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on("/api/config", HTTP_GET,
              [this, logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(
                    200, "application/json", _config->toJsonString());
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on(
      "/api/config", HTTP_POST,
      [this, logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        String body = extractPostBody(request);
        if (body.length() == 0)
          return;
        handleSetConfig(request, (uint8_t *)body.c_str(), body.length());
      },
      nullptr,
      // Upload handler for application/json
      [this, logRequest](AsyncWebServerRequest *request, uint8_t *data,
                         size_t len, size_t, size_t) {
        logRequest(request);
        String body = extractJsonBody(request, data, len);
        handleSetConfig(request, (uint8_t *)body.c_str(), body.length());
      });

  // Factory Reset API
  _server->on(
      "/api/factory_reset", HTTP_POST,
      [this, logRequest](AsyncWebServerRequest *request) {
        logRequest(request);
        bool ok = _config->factoryReset();
        if (ok) {
          AsyncWebServerResponse *resp =
              request->beginResponse(200, "application/json",
                                     "{\"success\":true,\"message\":\"Factory "
                                     "reset complete, rebooting...\"}");
          for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
            resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
          request->send(resp);
          request->onDisconnect([]() {
            delay(100);
            ESP.restart();
          });
        } else {
          AsyncWebServerResponse *resp = request->beginResponse(
              500, "application/json",
              "{\"success\":false,\"error\":\"Failed to delete config file\"}");
          for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
            resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
          request->send(resp);
        }
      });


  _server->on("/api/timer", HTTP_OPTIONS,
              [logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                AsyncWebServerResponse *resp = request->beginResponse(204);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
  _server->on("/api/timer", HTTP_POST, nullptr, nullptr,
              [this, logRequest](AsyncWebServerRequest *request, uint8_t *data,
                                 size_t len, size_t, size_t) {
                logRequest(request);
                handleSetTimer(request, data, len);
              });
  // Supported timezones API
  _server->on("/api/timezones", HTTP_GET,
              [this, logRequest](AsyncWebServerRequest *request) {
                logRequest(request);
                std::vector<String> tzList = _config->getSupportedTimezones();
                StaticJsonDocument<2048> namesDoc;
                JsonArray namesArr = namesDoc.to<JsonArray>();
                for (const auto &tz : tzList) {
                  namesArr.add(tz);
                }
                String json;
                serializeJson(namesArr, json);
                AsyncWebServerResponse *resp =
                    request->beginResponse(200, "application/json", json);
                for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
                  resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
                request->send(resp);
              });
}

void WebServerManager::handleGetState(AsyncWebServerRequest *request) {
  {
    AsyncWebServerResponse *resp =
        request->beginResponse(200, "application/json", getStateJSON());
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
  }
}

void WebServerManager::handleSetState(AsyncWebServerRequest *request,
                                      uint8_t *data, size_t len) {
  StaticJsonDocument<512> doc;
  String jsonStr = extractJsonBody(request, data, len);
  if (!parseJsonOrRespond(request, jsonStr, doc))
    return;

  // Only update fields present in the request
  bool updated = false;
  if (doc.containsKey("brightness")) {
    uint8_t brightness = percentToHex(doc["brightness"]);
    applyBrightnessLimit(brightness);
    applyTransitionTimeLimit(state.transitionTime);
    if (_brightnessCallback)
      _brightnessCallback(brightness);
    updated = true;
  }
  if (doc.containsKey("transitionTime")) {
    uint32_t transitionTime = (uint32_t)doc["transitionTime"];
    applyTransitionTimeLimit(transitionTime);
    applyBrightnessLimit(state.brightness);
    state.transitionTime = transitionTime;
    updated = true;
  }
  if (doc.containsKey("power")) {
    bool power = doc["power"];
    if (_powerCallback)
      _powerCallback(power);
    updated = true;
  }
  if (doc.containsKey("effect")) {
    uint8_t effect = (uint8_t)(int)doc["effect"];
    if (_effectCallback)
      _effectCallback(effect, state.params);
    updated = true;
  }
  if (doc.containsKey("params")) {
    JsonObject paramsObj = doc["params"];
    EffectParams params = state.params;
    if (paramsObj.containsKey("speed") && !paramsObj["speed"].isNull()) {
      params.speed =
          percentToHex((uint8_t)paramsObj["speed"]); // convert percent to 8-bit
      updated = true;
    }
    if (paramsObj.containsKey("intensity") &&
        !paramsObj["intensity"].isNull()) {
      params.intensity = percentToHex((uint8_t)paramsObj["intensity"]);
      updated = true;
    }
    if (paramsObj.containsKey("colors")) {
      JsonArray colorsArr = paramsObj["colors"].as<JsonArray>();
      std::vector<String> parsedColors;
      for (JsonVariant v : colorsArr) {
        if (v.is<const char *>()) {
          String hex = v.as<const char *>();
          if (hex.length() == 6 && hex[0] != '#') {
            hex = "#" + hex;
          }
          parsedColors.push_back(hex);
        }
      }
      params.colors = parsedColors;
      state.params.colors = parsedColors;
      updated = true;
    }
    if (updated && _effectCallback)
      _effectCallback(state.effect, params);
  }

  if (updated) {
    broadcastState();
  }
  AsyncWebServerResponse *resp =
      request->beginResponse(200, "application/json", "{\"success\":true}");
  for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
    resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
  request->send(resp);
}

void WebServerManager::handleGetPresets(AsyncWebServerRequest *request) {
  {
    AsyncWebServerResponse *resp =
        request->beginResponse(200, "application/json", getPresetsJSON());
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
  }
}

void WebServerManager::handleSetPreset(AsyncWebServerRequest *request,
                                       uint8_t *data, size_t len) {
#if defined(ESP32)
  try {
#endif
    StaticJsonDocument<512> doc;
    String jsonStr = extractJsonBody(request, data, len);
    if (!parseJsonOrRespond(request, jsonStr, doc))
      return;
    if (!doc.containsKey("id")) {
      AsyncWebServerResponse *resp = request->beginResponse(
          400, "application/json", "{\"error\":\"Missing preset ID\"}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
      return;
    }
    int reqId = doc["id"].as<int>();
    if (_config->presets.size() == 0) {
      AsyncWebServerResponse *resp = request->beginResponse(
          400, "application/json", "{\"error\":\"No presets available\"}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
      return;
    }
    auto it = std::find_if(_config->presets.begin(), _config->presets.end(),
                           [reqId](const Preset &p) { return p.id == reqId; });
    if (it == _config->presets.end()) {
      AsyncWebServerResponse *resp = request->beginResponse(
          400, "application/json", "{\"error\":\"Invalid preset ID\"}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
      return;
    }
    if (doc.containsKey("apply") && doc["apply"]) {
      if (_presetCallback)
        _presetCallback(it->id);
      AsyncWebServerResponse *resp =
          request->beginResponse(200, "application/json", "{\"success\":true}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
    } else {
      it->name = doc["name"] | "";
      it->effect = (uint8_t)(int)doc["effect"];
      it->enabled = doc["enabled"] | true;
      if (doc.containsKey("params")) {
        JsonObject paramsObj = doc["params"];
        it->params.speed = paramsObj["speed"].isNull()
                               ? percentToHex(100)
                               : percentToHex((uint8_t)paramsObj["speed"]);
        it->params.intensity =
            paramsObj["intensity"].isNull()
                ? percentToHex(50)
                : percentToHex((uint8_t)paramsObj["intensity"]);
        it->params.colors.clear();
        if (paramsObj.containsKey("colors")) {
          JsonArray colorsArr = paramsObj["colors"].as<JsonArray>();
          for (JsonVariant v : colorsArr) {
            if (v.is<const char *>()) {
              it->params.colors.push_back(String(v.as<const char *>()));
            }
          }
        }
      }
      savePresets(_config->presets);
      AsyncWebServerResponse *resp =
          request->beginResponse(200, "application/json", "{\"success\":true}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
    }
#if defined(ESP32)
  } catch (...) {
    AsyncWebServerResponse *resp = request->beginResponse(
        500, "application/json", "{\"error\":\"Internal server error\"}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
  }
#endif
}

void WebServerManager::handleSetConfig(AsyncWebServerRequest *request,
                                       uint8_t *data, size_t len) {
  // Parse uploaded JSON
  DynamicJsonDocument doc(4096);
  String jsonStr = extractJsonBody(request, data, len);
  if (!parseJsonOrRespond(request, jsonStr, doc))
    return;
  // Load current config from file for comparison
  DynamicJsonDocument currentDoc(4096);
  bool loaded = _config->loadFromFile(CONFIG_FILE, currentDoc);
  bool isDifferent = true;
  if (loaded) {
    String incoming, stored;
    serializeJson(doc, incoming);
    serializeJson(currentDoc, stored);
    isDifferent = (incoming != stored);
  }
  if (!isDifferent) {
    AsyncWebServerResponse *resp = request->beginResponse(
        200, "application/json",
        "{\"success\":true,\"message\":\"No changes detected\"}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
    return;
  }
  // Accept and persist SSID/password if present in network object
  if (doc.containsKey("network")) {
    JsonObject netObj = doc["network"];
    if (netObj.containsKey("ssid")) {
      _config->network.ssid = netObj["ssid"].as<String>();
    }
    if (netObj.containsKey("password")) {
      _config->network.password = netObj["password"].as<String>();
    }
  }
  // Only update fields present in the uploaded JSON
  _config->partialUpdate(doc.as<JsonObject>());
  bool saveResult = _config->save();
  if (saveResult) {
    if (_configCallback)
      _configCallback();
    AsyncWebServerResponse *resp =
        request->beginResponse(200, "application/json", "{\"success\":true}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
  } else {
    AsyncWebServerResponse *resp = request->beginResponse(
        500, "application/json",
        "{\"success\":false,\"error\":\"Failed to save config\"}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
  }
}


void WebServerManager::handleSetTimer(AsyncWebServerRequest *request,
                                      uint8_t *data, size_t len) {
  StaticJsonDocument<512> doc;
  String jsonStr = extractJsonBody(request, data, len);
  if (!parseJsonOrRespond(request, jsonStr, doc))
    return;

  uint8_t timerId = doc["id"] | 0;

  if (timerId >= _config->timers.size()) {
    {
      AsyncWebServerResponse *resp = request->beginResponse(
          400, "application/json", "{\"error\":\"Invalid timer ID\"}");
      for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
        resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
      request->send(resp);
    }
    return;
  }

  _config->timers[timerId].enabled = doc["enabled"] | false;
  _config->timers[timerId].type = (TimerType)(int)doc["type"];
  _config->timers[timerId].hour = doc["hour"] | 0;
  _config->timers[timerId].minute = doc["minute"] | 0;
  _config->timers[timerId].presetId = doc["presetId"] | 0;
  // Always store timer brightness as hex internally; convert from percent at
  // API boundary
  if (doc.containsKey("brightness")) {
    uint8_t percent = doc["brightness"] | 100;
    _config->timers[timerId].brightness = percentToHex(percent);
  }

  _config->save();

  {
    AsyncWebServerResponse *resp =
        request->beginResponse(200, "application/json", "{\"success\":true}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i)
      resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
  }
}

String WebServerManager::getStateJSON() {
  StaticJsonDocument<512> doc;
  if (state.inTransition) {
    doc["power"] = true;
    doc["effect"] = pendingTransition.effect;
    doc["preset"] = pendingTransition.preset;
    JsonObject paramsObj = doc.createNestedObject("params");
    paramsObj["speed"] = hexToPercent(pendingTransition.params.speed);
    paramsObj["intensity"] = hexToPercent(pendingTransition.params.intensity);
    JsonArray colorsArr = paramsObj.createNestedArray("colors");
    for (const auto &c : pendingTransition.params.colors) {
      colorsArr.add(c);
    }
  } else {
    doc["power"] = state.power;
    doc["effect"] = state.effect;
    doc["preset"] = state.preset;
    JsonObject paramsObj = doc.createNestedObject("params");
    paramsObj["speed"] = hexToPercent(state.params.speed);
    paramsObj["intensity"] = hexToPercent(state.params.intensity);
    JsonArray colorsArr = paramsObj.createNestedArray("colors");
    for (const auto &c : state.params.colors) {
      colorsArr.add(c);
    }
  }
  // These fields are always reported from state/transition engine
  doc["brightness"] = hexToPercent(transition._targetState.brightness);
  doc["transitionTime"] = state.transitionTime;
  doc["time"] =
      _scheduler->isTimeValid() ? _scheduler->getCurrentTime() : "--:--";
  doc["sunrise"] = _scheduler->getSunriseTime();
  doc["sunset"] = _scheduler->getSunsetTime();

  String output;
  serializeJson(doc, output);
  return output;
}

String WebServerManager::getPresetsJSON() {
  StaticJsonDocument<4096> doc;
  JsonArray presetsArray = doc.createNestedArray("presets");
  for (size_t i = 0; i < _config->getPresetCount(); i++) {
    if (_config->presets[i].name.length() == 0 && i > 0)
      continue;
    const auto &preset = _config->presets[i];
    JsonObject presetObj = presetsArray.createNestedObject();
    presetObj["id"] = i;
    presetObj["name"] = preset.name;
    presetObj["effect"] = preset.effect;
    presetObj["enabled"] = preset.enabled;
    JsonObject paramsObj = presetObj.createNestedObject("params");
    paramsObj["speed"] = preset.params.speed;
    paramsObj["intensity"] = hexToPercent(preset.params.intensity);
    JsonArray colorsArr = paramsObj.createNestedArray("colors");
    if (preset.params.colors.size() > 0) {
      for (const auto &c : preset.params.colors) {
        colorsArr.add(c);
      }
    }
  }
  String output;
  serializeJson(doc, output);
  return output;
}

String WebServerManager::getTimersJSON() {
  StaticJsonDocument<2048> doc;
  JsonArray timersArray = doc.createNestedArray("timers");

  for (size_t i = 0; i < _config->timers.size(); i++) {
    // Only include timers that are enabled or have a nonzero hour/minute or
    // non-empty name
    const auto &t = _config->timers[i];
    bool isActive = t.enabled || t.hour != 0 || t.minute != 0;
#ifdef TIMER_NAME_SUPPORT
    isActive = isActive || (t.name && t.name[0] != '\0');
#endif
    if (!isActive)
      continue;
    JsonObject timerObj = timersArray.createNestedObject();
    timerObj["id"] = i;
    timerObj["enabled"] = t.enabled;
    timerObj["type"] = t.type;
    timerObj["hour"] = t.hour;
    timerObj["minute"] = t.minute;
    timerObj["presetId"] = t.presetId;
    timerObj["brightness"] = hexToPercent(t.brightness);
  }

  String output;
  serializeJson(doc, output);
  return output;
}

bool WebServerManager::applyBrightnessLimit(uint8_t &brightness) {
  if (brightness > _config->safety.maxBrightness) {
    brightness = _config->safety.maxBrightness;
    return true;
  }
  return false;
}

bool WebServerManager::applyTransitionTimeLimit(uint32_t &transitionTime) {
  if (transitionTime < _config->safety.minTransitionTime) {
    transitionTime = _config->safety.minTransitionTime;
    return true;
  }
  return false;
}

void WebServerManager::broadcastState() {
  // Sync config.state.brightness with transition engine before broadcasting
  state.brightness = transition._currentState.brightness;
  String stateJSON = getStateJSON();
  if (_ws) {
    for (auto &client : _ws->getClients()) {
      // If client is OTA-subscribed, skip sending state updates
      if (otaSubscribedClients.count(client.id())) continue;
      client.text(stateJSON);
    }
  }
}

void WebServerManager::onPowerChange(void (*callback)(bool)) {
  _powerCallback = callback;
}

void WebServerManager::onBrightnessChange(void (*callback)(uint8_t)) {
  _brightnessCallback = callback;
}

void WebServerManager::onEffectChange(void (*callback)(uint8_t,
                                                       const EffectParams &)) {
  _effectCallback = callback;
}

void WebServerManager::onPresetApply(void (*callback)(uint8_t)) {
  _presetCallback = callback;
}

void WebServerManager::onConfigChange(void (*callback)()) {
  _configCallback = callback;
}
// Call this when OTA finishes (success or error) to unsubscribe all OTA clients
void WebServerManager::clearOtaSubscriptions() {
  otaSubscribedClients.clear();
}

// Close all OTA WebSocket clients (disconnect them)
void WebServerManager::closeOtaClients() {
  if (_ws) {
    for (auto &client : _ws->getClients()) {
      if (otaSubscribedClients.count(client.id())) {
        client.close();
      }
    }
  }
}

// Return the number of currently connected OTA WebSocket clients
int WebServerManager::otaClientsConnected() const {
  int count = 0;
  if (_ws) {
    for (auto &client : _ws->getClients()) {
      if (otaSubscribedClients.count(client.id())) {
        ++count;
      }
    }
  }
  return count;
}
