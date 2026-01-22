#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR)
#include <pgmspace.h>
#endif
#include "inc/index_html.inc"
#include "inc/wifi_html.inc"
#include "inc/app_js.inc"
#include "inc/style_css.inc"
#include "inc/fflate_min_js.inc"
#include "inc/config_html.inc"
#include "inc/config_js.inc"

#include <Ticker.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#if defined(ESP32)
#include <Update.h>
#elif defined(ESP8266)
#include <Updater.h>
#endif

#include "effects.h"
#include "transition.h"
#include "presets.h"
#include "state.h"
#include "version.h"
#include "ota.h"
#include "webserver.h"

// Stub implementations for OTA memory management hooks
void pauseEffects() {}
void pauseWebServer() {}
void resumeEffects() {}
void resumeWebServer() {}

// Helper: CORS headers for API responses
static const char* CORS_HEADERS[][2] = {
    {"Access-Control-Allow-Origin", "*"},
    {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
    {"Access-Control-Allow-Headers", "Content-Type"}
};
static const size_t CORS_HEADER_COUNT = sizeof(CORS_HEADERS) / sizeof(CORS_HEADERS[0]);

extern TransitionEngine transition;
extern SystemState state;

// Helper: URL decode for form fields (declaration)
static String urlDecode(const String& input);

// Cached effect list JSON
static String cachedEffectsJson;
static bool effectsCacheReady = false;

// --- OTA Status WebSocket Broadcast ---
void WebServerManager::broadcastOtaStatus(const String& status, const String& message, int progress) {
    StaticJsonDocument<256> doc;
    doc["type"] = "ota_status";
    doc["status"] = status;
    if (message.length() > 0) doc["message"] = message;
    if (progress >= 0) doc["progress"] = progress;
    String json;
    serializeJson(doc, json);
    if (_ws) _ws->textAll(json);
}

// Helper: Extract JSON body from POST request (for upload handler)
static String extractJsonBody(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    String jsonStr;
    if (len > 0 && data != nullptr) {
        for (size_t i = 0; i < len; ++i) jsonStr += (char)data[i];
    } else {
        jsonStr = request->arg("plain");
    }
    return jsonStr;
}

// Helper: Extract POST body for main POST handler
static String extractPostBody(AsyncWebServerRequest* request) {
    String body = request->arg("plain");
    if (body.length() == 0 && request->params() > 0) {
        body = request->getParam((size_t)0)->value();
    }
    if (body.length() == 0) {
        if (request->_tempObject && ((String*)request->_tempObject)->length() > 0) {
            body = *((String*)request->_tempObject);
        }
    }
    return body;
}

// Helper: Deserialize JSON and handle error response
template<typename TDoc>
static bool parseJsonOrRespond(AsyncWebServerRequest* request, const String& jsonStr, TDoc& doc) {
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        return false;
    }
    return true;
}

void buildEffectsCache() {
    // Populate effects array from the effect registry (portable vector-based)
    StaticJsonDocument<4096> doc;
    JsonArray effects = doc.createNestedArray("effects");
    extern std::vector<EffectRegistryEntry> effectRegistry;
    for (size_t i = 0; i < effectRegistry.size(); ++i) {
        JsonObject eff = effects.createNestedObject();
        eff["id"] = effectRegistry[i].id;
        eff["name"] = effectRegistry[i].name;
    }
    cachedEffectsJson.clear();
    serializeJson(doc, cachedEffectsJson);
    effectsCacheReady = true;
}

static String urlDecode(const String& input) {
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


WebServerManager::WebServerManager(Configuration* config, Scheduler* scheduler) {
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
}

void WebServerManager::update() {
    _ws->cleanupClients();
    // No periodic broadcast; state is sent only on connection and on actual changes
}

void WebServerManager::setupWebSocket() {
    _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                     AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            // Send current state immediately to the new client
            client->text(getStateJSON());
        } else if (type == WS_EVT_DISCONNECT) {
        }
    });
    _server->addHandler(_ws);
}




void WebServerManager::setupRoutes() {

    // Debug: Log every incoming HTTP request
    _server->onNotFound([this](AsyncWebServerRequest* request) {
        debugPrintln(String("[HTTP] NotFound: ") + request->url());
        request->send(404, "text/plain", "Not Found");
    });

    // Helper: Log all requests (valid or not)
    auto logRequest = [](AsyncWebServerRequest* request) {
        String logMsg = String("[HTTP] ") + (request->method() == HTTP_GET ? "GET " : request->method() == HTTP_POST ? "POST " : "OTHER ") + request->url();
        debugPrintln(logMsg);
    };
    // Version API endpoint
    _server->on("/api/version", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", String("{\"version\":\"") + getFirmwareVersion() + "\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    // Update API endpoint: POST /api/update (OTA with .bin.gz support)
    _server->on("/api/update", HTTP_POST, [logRequest, this](AsyncWebServerRequest* request) {
        logRequest(request);
        debugPrintln("[OTA] /api/update called. Launching OTA FreeRTOS task.");
        static const char* firmwareUrl = "https://github.com/kabroxiko/DeepGlow/releases/download/v1.0.0/firmware_esp32d_debug_1.0.0.bin.gz";
    #if defined(ESP32)
        Serial.printf("[OTA] Free heap before OTA: %u\n", ESP.getFreeHeap());
        xTaskCreatePinnedToCore(
            otaTask, "otaTask", 16384, (void*)firmwareUrl, 1, nullptr, 1);
    #endif
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"OTA update started in background. Device will update and reboot.\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });

    // System command API (reboot, update, etc.)
    _server->on("/api/command", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/command", HTTP_POST, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        request->onDisconnect([]() { delay(100); ESP.restart(); });
    },
        NULL,
        [logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            logRequest(request);
            StaticJsonDocument<128> doc;
            DeserializationError error = deserializeJson(doc, data, len);
            String respJson;
            int status = 200;
            if (error || !doc.containsKey("command")) {
                respJson = "{\"success\":false,\"error\":\"Invalid JSON or missing command\"}";
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
            AsyncWebServerResponse *resp = request->beginResponse(status, "application/json", respJson);
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
            // Actually perform the command after response
            if (!error && doc.containsKey("command")) {
                String cmd = doc["command"].as<String>();
                if (cmd == "reboot") {
                    request->onDisconnect([]() { delay(100); ESP.restart(); });
                }
            }
        }
    );
    // CORS preflight for OTA
    _server->on("/ota", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    // OTA Update endpoint (POST /ota, direct binary upload)
    _server->on("/ota", HTTP_POST,
        [logRequest](AsyncWebServerRequest* request) {
            logRequest(request);
            AsyncWebServerResponse *resp = nullptr;
            if (Update.hasError()) {
                resp = request->beginResponse(500, "application/json", "{\"error\":\"OTA Update Failed\"}");
            } else {
                resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
            }
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
            if (!Update.hasError()) {
                request->onDisconnect([]() {
                    delay(100);
                    ESP.restart();
                });
            }
        },
        NULL,
        [logRequest](AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total) {
            handleOTAUpdate(request, reinterpret_cast<uint8_t*>(data), static_cast<size_t>(len), static_cast<size_t>(index), static_cast<size_t>(total));
        }
    );

    // Captive portal triggers for auto-popup on phones/laptops
    _server->on("/generate_204", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->redirect("/wifi");
    });
    _server->on("/hotspot-detect.html", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->redirect("/wifi");
    });
    _server->on("/ncsi.txt", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->redirect("/wifi");
    });
    _server->on("/connecttest.txt", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->redirect("/wifi");
    });
    // Extra captive portal triggers for maximum compatibility
    _server->on("/favicon.ico", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send(204); // No Content
    });
    _server->on("/wpad.dat", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send(204); // No Content
    });

    // Serve web assets from filesystem image
    _server->on("/", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
    });
    _server->on("/index.html", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
    });
    // Serve WiFi page for POST: robust handler parses body manually
    _server->on("/wifi", HTTP_POST, 
        [this, logRequest](AsyncWebServerRequest* request) {
            logRequest(request);
            for (size_t i = 0; i < request->params(); i++) {
            }
            // Fallback: If body handler is not called, parse POST params here
            if (request->hasParam("ssid", true)) {
                String ssid = urlDecode(request->getParam("ssid", true)->value());
                String password = request->hasParam("password", true) ? urlDecode(request->getParam("password", true)->value()) : "";
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
                request->send_P(200, "text/html", web_wifi_html, web_wifi_html_len);
            }
        }, 
        nullptr,
        [this, logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            logRequest(request);
            String body;
            for (size_t i = 0; i < len; ++i) body += (char)data[i];
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
            request->send_P(200, "text/html", web_wifi_html, web_wifi_html_len);
        }
    );
    // For GET, serve the WiFi form
    _server->on("/wifi", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "text/html", web_wifi_html, web_wifi_html_len);
    });
    _server->on("/app.js", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "application/javascript", web_app_js, web_app_js_len);
    });
    _server->on("/config.html", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "text/html", web_config_html, web_config_html_len);
    });
    _server->on("/config.js", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "application/javascript", web_config_js, web_config_js_len);
    });
    _server->on("/style.css", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "text/css", web_style_css, web_style_css_len);
    });
    _server->on("/fflate.min.js", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        request->send_P(200, "application/javascript", web_fflate_min_js, web_fflate_min_js_len);
    });
    
    // State API
    _server->on("/api/state", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/state", HTTP_GET, [this, logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        handleGetState(request);
    });
    
    // Main POST handler
    _server->on("/api/state", HTTP_POST,
        [this, logRequest](AsyncWebServerRequest* request) {
            logRequest(request);
            String body = extractPostBody(request);
            if (body.length() == 0) return;
            handleSetState(request, (uint8_t*)body.c_str(), body.length());
        },
        nullptr,
        // Upload handler for application/json
        [this, logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            logRequest(request);
            String body = extractJsonBody(request, data, len);
            handleSetState(request, (uint8_t*)body.c_str(), body.length());
        }
    );

    // Effects API: serve cached JSON for all available predefined effect names and indices
    _server->on("/api/effects", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        if (!effectsCacheReady) buildEffectsCache();
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", cachedEffectsJson);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });

    // Presets API
    _server->on("/api/presets", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/presets", HTTP_GET, [this, logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        handleGetPresets(request);
    });
    
    _server->on("/api/preset", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/preset", HTTP_POST,
        [this, logRequest](AsyncWebServerRequest* request) {
            logRequest(request);
            String body = extractPostBody(request);
            if (body.length() == 0) return; // Prevent double response if upload handler already processed
            handleSetPreset(request, (uint8_t*)body.c_str(), body.length());
        },
        nullptr,
        // Upload handler for application/json
        [this, logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            logRequest(request);
            String body = extractJsonBody(request, data, len);
            handleSetPreset(request, (uint8_t*)body.c_str(), body.length());
        }
    );
    
    // Configuration API
    _server->on("/api/config", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/config", HTTP_GET, [this, logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", _config->toJsonString());
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/config", HTTP_POST,
        [this, logRequest](AsyncWebServerRequest* request) {
            logRequest(request);
            String body = extractPostBody(request);
            if (body.length() == 0) return;
            handleSetConfig(request, (uint8_t*)body.c_str(), body.length());
        },
        nullptr,
        // Upload handler for application/json
        [this, logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            logRequest(request);
            String body = extractJsonBody(request, data, len);
            handleSetConfig(request, (uint8_t*)body.c_str(), body.length());
        }
    );

    // Factory Reset API
    _server->on("/api/factory_reset", HTTP_POST, [this, logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        bool ok = _config->factoryReset();
        if (ok) {
            AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Factory reset complete, rebooting...\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
            request->onDisconnect([]() { delay(100); ESP.restart(); });
        } else {
            AsyncWebServerResponse *resp = request->beginResponse(500, "application/json", "{\"success\":false,\"error\":\"Failed to delete config file\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
    });

    // Timers API
    _server->on("/api/timers", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/timers", HTTP_GET, [this, logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        handleGetTimers(request);
    });
    
    _server->on("/api/timer", HTTP_OPTIONS, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/timer", HTTP_POST, nullptr, nullptr,
        [this, logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            logRequest(request);
            handleSetTimer(request, data, len);
        }
    );
    // Supported timezones API
    _server->on("/api/timezones", HTTP_GET, [this, logRequest](AsyncWebServerRequest* request) {
        logRequest(request);
        std::vector<String> tzList = _config->getSupportedTimezones();
        StaticJsonDocument<2048> namesDoc;
        JsonArray namesArr = namesDoc.to<JsonArray>();
        for (const auto& tz : tzList) {
            namesArr.add(tz);
        }
        String json;
        serializeJson(namesArr, json);
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", json);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
}

void WebServerManager::handleGetState(AsyncWebServerRequest* request) {
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", getStateJSON());
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

void WebServerManager::handleSetState(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    StaticJsonDocument<512> doc;
    String jsonStr = extractJsonBody(request, data, len);
    if (!parseJsonOrRespond(request, jsonStr, doc)) return;

    // Only update fields present in the request
    bool updated = false;
    if (doc.containsKey("brightness")) {
        uint8_t brightness = percentToHex(doc["brightness"]);
        applyBrightnessLimit(brightness);
        applyTransitionTimeLimit(state.transitionTime);
        if (_brightnessCallback) _brightnessCallback(brightness);
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
        if (_powerCallback) _powerCallback(power);
        updated = true;
    }
    if (doc.containsKey("effect")) {
        uint8_t effect = (uint8_t)(int)doc["effect"];
        if (_effectCallback) _effectCallback(effect, state.params);
        updated = true;
    }
    if (doc.containsKey("params")) {
        JsonObject paramsObj = doc["params"];
        EffectParams params = state.params;
        if (paramsObj.containsKey("speed") && !paramsObj["speed"].isNull()) {
            params.speed = (uint8_t)paramsObj["speed"];
            updated = true;
        }
        if (paramsObj.containsKey("intensity") && !paramsObj["intensity"].isNull()) {
            params.intensity = (uint8_t)paramsObj["intensity"];
            updated = true;
        }
        if (paramsObj.containsKey("colors")) {
            JsonArray colorsArr = paramsObj["colors"].as<JsonArray>();
            std::vector<String> parsedColors;
            for (JsonVariant v : colorsArr) {
                if (v.is<const char*>()) {
                    String hex = v.as<const char*>();
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
        if (updated && _effectCallback) _effectCallback(state.effect, params);
    }

    if (updated) {
        broadcastState();
    }
    AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
    for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
    request->send(resp);
}

void WebServerManager::handleGetPresets(AsyncWebServerRequest* request) {
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", getPresetsJSON());
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

void WebServerManager::handleSetPreset(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    #if defined(ESP32)
    try {
    #endif
        StaticJsonDocument<512> doc;
        String jsonStr = extractJsonBody(request, data, len);
        if (!parseJsonOrRespond(request, jsonStr, doc)) return;
        if (!doc.containsKey("id")) {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Missing preset ID\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
            return;
        }
        int reqId = doc["id"].as<int>();
        if (_config->presets.size() == 0) {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"No presets available\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
            return;
        }
        auto it = std::find_if(_config->presets.begin(), _config->presets.end(), [reqId](const Preset& p) { return p.id == reqId; });
        if (it == _config->presets.end()) {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid preset ID\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
            return;
        }
        if (doc.containsKey("apply") && doc["apply"]) {
            if (_presetCallback) _presetCallback(it->id);
            AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        } else {
            it->name = doc["name"] | "";
            it->effect = (uint8_t)(int)doc["effect"];
            it->enabled = doc["enabled"] | true;
            if (doc.containsKey("params")) {
                JsonObject paramsObj = doc["params"];
                it->params.speed = paramsObj["speed"].isNull() ? 100 : (uint8_t)paramsObj["speed"];
                it->params.intensity = paramsObj["intensity"] | 128;
                it->params.colors.clear();
                if (paramsObj.containsKey("colors")) {
                    JsonArray colorsArr = paramsObj["colors"].as<JsonArray>();
                    for (JsonVariant v : colorsArr) {
                        if (v.is<const char*>()) {
                            it->params.colors.push_back(String(v.as<const char*>()));
                        }
                    }
                }
            }
            savePresets(_config->presets);
            AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
    #if defined(ESP32)
    } catch (...) {
        AsyncWebServerResponse *resp = request->beginResponse(500, "application/json", "{\"error\":\"Internal server error\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
    #endif
}



void WebServerManager::handleSetConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    // Parse uploaded JSON
    DynamicJsonDocument doc(4096);
    String jsonStr = extractJsonBody(request, data, len);
    if (!parseJsonOrRespond(request, jsonStr, doc)) return;
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
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"No changes detected\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
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
        if (_configCallback) _configCallback();
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    } else {
        AsyncWebServerResponse *resp = request->beginResponse(500, "application/json", "{\"success\":false,\"error\":\"Failed to save config\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

void WebServerManager::handleGetTimers(AsyncWebServerRequest* request) {
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", getTimersJSON());
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

void WebServerManager::handleSetTimer(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    StaticJsonDocument<512> doc;
    String jsonStr = extractJsonBody(request, data, len);
    if (!parseJsonOrRespond(request, jsonStr, doc)) return;
    
    uint8_t timerId = doc["id"] | 0;
    
    if (timerId >= _config->timers.size()) {
        {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid timer ID\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
        return;
    }
    
    _config->timers[timerId].enabled = doc["enabled"] | false;
    _config->timers[timerId].type = (TimerType)(int)doc["type"];
    _config->timers[timerId].hour = doc["hour"] | 0;
    _config->timers[timerId].minute = doc["minute"] | 0;
    _config->timers[timerId].presetId = doc["presetId"] | 0;
    // Always store timer brightness as hex internally; convert from percent at API boundary
    if (doc.containsKey("brightness")) {
        uint8_t percent = doc["brightness"] | 100;
        _config->timers[timerId].brightness = percentToHex(percent);
    }
    
    _config->save();
    
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

String WebServerManager::getStateJSON() {
    StaticJsonDocument<512> doc;
    extern TransitionEngine transition;
    extern PendingTransitionState pendingTransition;
    // Only use pendingTransition for fields that actually change during a transition
    if (state.inTransition) {
        doc["power"] = true;
        doc["effect"] = pendingTransition.effect;
        doc["preset"] = pendingTransition.preset;
        JsonObject paramsObj = doc.createNestedObject("params");
        paramsObj["speed"] = pendingTransition.params.speed;
        paramsObj["intensity"] = pendingTransition.params.intensity;
        JsonArray colorsArr = paramsObj.createNestedArray("colors");
        for (const auto& c : pendingTransition.params.colors) {
            colorsArr.add(c);
        }
    } else {
        doc["power"] = state.power;
        doc["effect"] = state.effect;
        doc["preset"] = state.preset;
        JsonObject paramsObj = doc.createNestedObject("params");
        paramsObj["speed"] = state.params.speed;
        paramsObj["intensity"] = state.params.intensity;
        JsonArray colorsArr = paramsObj.createNestedArray("colors");
        for (const auto& c : state.params.colors) {
            colorsArr.add(c);
        }
    }
    // These fields are always reported from state/transition engine
    doc["brightness"] = hexToPercent(transition.getTargetBrightness());
    doc["transitionTime"] = state.transitionTime;
    doc["time"] = _scheduler->isTimeValid() ? _scheduler->getCurrentTime() : "--:--";
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
        if (_config->presets[i].name.length() == 0 && i > 0) continue;
        const auto& preset = _config->presets[i];
        JsonObject presetObj = presetsArray.createNestedObject();
        presetObj["id"] = i;
        presetObj["name"] = preset.name;
        presetObj["effect"] = preset.effect;
        presetObj["enabled"] = preset.enabled;
        JsonObject paramsObj = presetObj.createNestedObject("params");
        paramsObj["speed"] = preset.params.speed;
        paramsObj["intensity"] = preset.params.intensity;
        JsonArray colorsArr = paramsObj.createNestedArray("colors");
        if (preset.params.colors.size() > 0) {
            for (const auto& c : preset.params.colors) {
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
        // Only include timers that are enabled or have a nonzero hour/minute or non-empty name
        const auto& t = _config->timers[i];
        bool isActive = t.enabled || t.hour != 0 || t.minute != 0;
    #ifdef TIMER_NAME_SUPPORT
        isActive = isActive || (t.name && t.name[0] != '\0');
    #endif
        if (!isActive) continue;
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

bool WebServerManager::applyBrightnessLimit(uint8_t& brightness) {
    if (brightness > _config->safety.maxBrightness) {
        brightness = _config->safety.maxBrightness;
        return true;
    }
    return false;
}

bool WebServerManager::applyTransitionTimeLimit(uint32_t& transitionTime) {
    if (transitionTime < _config->safety.minTransitionTime) {
        transitionTime = _config->safety.minTransitionTime;
        return true;
    }
    return false;
}

void WebServerManager::broadcastState() {
    // Sync config.state.brightness with transition engine before broadcasting
    extern TransitionEngine transition;
    // Store as percent for reporting
    state.brightness = transition.getCurrentBrightness();
    String stateJSON = getStateJSON();
    _ws->textAll(stateJSON);
}

void WebServerManager::onPowerChange(void (*callback)(bool)) {
    _powerCallback = callback;
}

void WebServerManager::onBrightnessChange(void (*callback)(uint8_t)) {
    _brightnessCallback = callback;
}

void WebServerManager::onEffectChange(void (*callback)(uint8_t, const EffectParams&)) {
    _effectCallback = callback;
}

void WebServerManager::onPresetApply(void (*callback)(uint8_t)) {
    _presetCallback = callback;
}

void WebServerManager::onConfigChange(void (*callback)()) {
    _configCallback = callback;
}
