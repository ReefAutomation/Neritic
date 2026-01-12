#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR)
#include <pgmspace.h>
#endif
#include "web_assets/index_html.inc"
#include "web_assets/wifi_html.inc"
#include "web_assets/app_js.inc"
#include "web_assets/style_css.inc"
#include "web_assets/fflate_min_js.inc"
#include "web_assets/config_html.inc"
#include "web_assets/config_js.inc"

#include "webserver.h"
#if defined(ESP32)
#include <Update.h>
#elif defined(ESP8266)
#include <Updater.h>
#endif
#include "effects.h"
#include "debug.h"
#include "transition.h"
#include "presets.h"
#include "state.h"



// Cached effect list JSON
static String cachedEffectsJson;
static bool effectsCacheReady = false;

void buildEffectsCache() {
    // Populate effects array from the effect registry (portable vector-based)
    StaticJsonDocument<4096> doc;
    JsonArray effects = doc.createNestedArray("effects");
    const auto& reg = getEffectRegistry();
    for (size_t i = 0; i < reg.size(); ++i) {
        JsonObject eff = effects.createNestedObject();
        eff["id"] = i;
        eff["name"] = reg[i].name;
    }
    cachedEffectsJson.clear();
    serializeJson(doc, cachedEffectsJson);
    effectsCacheReady = true;
}
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

// Place at the very end of the file, after all other code
void WebServerManager::handleOTAUpdate(AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total) {
    // Actual OTA update logic
    static unsigned int lastDot = 0;
    if (index == 0) {
    #if defined(ESP32)
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    #elif defined(ESP8266)
        if (!Update.begin(total)) {
    #endif
            Update.printError(Serial);
        }
    }
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
    }
    // Print progress dots every 10%
    if (total > 0) {
        unsigned int dot = ((index + len) * 100) / total;
        while (lastDot < dot) {
            lastDot++;
        }
    }
    if (index + len == total) {
        lastDot = 0; // reset for next OTA
        bool ok = Update.end(true);
        AsyncWebServerResponse *resp = nullptr;
        if (ok) {
            resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
        } else {
            Update.printError(Serial);
            resp = request->beginResponse(500, "application/json", "{\"error\":\"OTA Update Failed\"}");
        }
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        if (ok) {
            request->onDisconnect([]() {
                delay(100);
                ESP.restart();
            });
        }
    }
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
    // System command API (reboot, update, etc.)
    _server->on("/api/command", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/command", HTTP_POST, [](AsyncWebServerRequest* request) {
        // If body handler is not called, treat as reboot for compatibility
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        request->onDisconnect([]() { delay(100); ESP.restart(); });
    },
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
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
    _server->on("/ota", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    // OTA Update endpoint (POST /ota, direct binary upload)
    _server->on("/ota", HTTP_POST,
        [](AsyncWebServerRequest* request) {
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
        [this](AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total) {
            handleOTAUpdate(request, reinterpret_cast<uint8_t*>(data), static_cast<size_t>(len), static_cast<size_t>(index), static_cast<size_t>(total));
        }
    );

    // Captive portal triggers for auto-popup on phones/laptops
    auto logRequest = [](AsyncWebServerRequest* request, const char* tag = "[DEBUG] HTTP") {
    };
    _server->on("/generate_204", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /generate_204");
        request->redirect("/wifi");
    });
    _server->on("/hotspot-detect.html", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /hotspot-detect.html");
        request->redirect("/wifi");
    });
    _server->on("/ncsi.txt", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /ncsi.txt");
        request->redirect("/wifi");
    });
    _server->on("/connecttest.txt", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /connecttest.txt");
        request->redirect("/wifi");
    });
    // Extra captive portal triggers for maximum compatibility
    _server->on("/favicon.ico", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /favicon.ico");
        request->send(204); // No Content
    });
    _server->on("/wpad.dat", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /wpad.dat");
        request->send(204); // No Content
    });

    // Serve web assets from filesystem image
    _server->on("/", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /");
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
    });
    _server->on("/index.html", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /index.html");
        request->send_P(200, "text/html", web_index_html, web_index_html_len);
    });
    // Serve WiFi page for POST: robust handler parses body manually (WLED-style, minimal signature)
    _server->on("/wifi", HTTP_POST, 
        [this, logRequest](AsyncWebServerRequest* request) {
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
        for (size_t i = 0; i < request->headers(); i++) {
            debugPrint("    ");
            debugPrint(request->headerName(i));
            debugPrint(": ");
            debugPrintln(request->header(i));
        }
        request->send_P(200, "text/html", web_wifi_html, web_wifi_html_len);
    });
    _server->on("/app.js", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /app.js");
        request->send_P(200, "application/javascript", web_app_js, web_app_js_len);
    });
    _server->on("/config.html", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /config.html");
        request->send_P(200, "text/html", web_config_html, web_config_html_len);
    });
    _server->on("/config.js", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /config.js");
        request->send_P(200, "application/javascript", web_config_js, web_config_js_len);
    });
    _server->on("/style.css", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /style.css");
        request->send_P(200, "text/css", web_style_css, web_style_css_len);
    });
    _server->on("/fflate.min.js", HTTP_GET, [logRequest](AsyncWebServerRequest* request) {
        logRequest(request, "[DEBUG] /fflate.min.js");
        request->send_P(200, "application/javascript", web_fflate_min_js, web_fflate_min_js_len);
    });
    
    // State API
    _server->on("/api/state", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetState(request);
    });
    
    _server->on("/api/state", HTTP_POST, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    },
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleSetState(request, data, len);
    });

    // Effects API: serve cached JSON for all available predefined effect names and indices
    _server->on("/api/effects", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!effectsCacheReady) buildEffectsCache();
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", cachedEffectsJson);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });

    // Presets API
    _server->on("/api/presets", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/presets", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetPresets(request);
    });
    
    _server->on("/api/preset", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/preset", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Always return a JSON response so frontend .json() does not fail
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    },
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleSetPreset(request, data, len);
    });
    
    // Configuration API
    _server->on("/api/config", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetConfig(request);
    });
    
    _server->on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    },
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleSetConfig(request, data, len);
    });
    // Factory Reset API
    _server->on("/api/factory_reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
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
    _server->on("/api/timers", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/timers", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetTimers(request);
    });
    
    _server->on("/api/timer", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    });
    _server->on("/api/timer", HTTP_POST, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse *resp = request->beginResponse(204);
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    },
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleSetTimer(request, data, len);
    });
    // Supported timezones API
    _server->on("/api/timezones", HTTP_GET, [this](AsyncWebServerRequest* request) {
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
        debugPrintln("[API] handleSetState called");
        debugPrint("[API] Raw data: ");
        for (size_t i = 0; i < len; ++i) debugPrint((char)data[i]);
        debugPrintln("");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        return;
    }

    // Only update fields present in the request
    bool updated = false;
    if (doc.containsKey("brightness")) {
        uint8_t brightness = doc["brightness"];
        applySafetyLimits(brightness, state.transitionTime);
        if (_brightnessCallback) _brightnessCallback(brightness);
        updated = true;
    }
    if (doc.containsKey("transitionTime")) {
        uint32_t transitionTime = (uint32_t)doc["transitionTime"];
        applySafetyLimits(state.brightness, transitionTime);
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
            uint32_t arr[8] = {0x0000FF, 0x00FFFF};
            size_t n = colorsArr.size() > 8 ? 8 : colorsArr.size();
            for (size_t i = 0; i < n; ++i) {
                const char* hex = colorsArr[i];
                if (hex[0] == '#') hex++;
                // Always parse exactly 6 hex digits for RGB
                char buf[7] = {0};
                strncpy(buf, hex, 6);
                arr[i] = ((uint32_t)strtoul(buf, nullptr, 16)) & 0xFFFFFF;
            }
            setUserColor(arr, n);
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
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
        return;
    }
    
    int reqId = doc["id"] | -1;
    auto it = std::find_if(_config->presets.begin(), _config->presets.end(), [reqId](const Preset& p) { return p.id == reqId; });
    if (it == _config->presets.end()) {
        AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid preset ID\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        return;
    }
    // Apply or save preset
    if (doc.containsKey("apply") && doc["apply"]) {
        if (_presetCallback) _presetCallback(it->id);
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    } else {
        // Save preset data
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
}

void WebServerManager::handleGetConfig(AsyncWebServerRequest* request) {
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", getConfigJSON());
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

void WebServerManager::handleSetConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    // Parse uploaded JSON
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
        return;
    }
    // Load current config from file for comparison
    DynamicJsonDocument currentDoc(4096);
    bool loaded = _config->loadFromFile(CONFIG_FILE, currentDoc);
    bool isDifferent = true;
    if (loaded) {
        String incoming, stored;
        serializeJson(doc, incoming);
        serializeJson(currentDoc, stored);
        Serial.println("[DEBUG] Incoming config JSON:");
        Serial.println(incoming);
        Serial.println("[DEBUG] Current config JSON:");
        Serial.println(stored);
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
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
        return;
    }
    
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
    _config->timers[timerId].brightness = doc["brightness"] | 100;
    
    _config->save();
    
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

String WebServerManager::getStateJSON() {
    StaticJsonDocument<512> doc;
    
    doc["power"] = state.power;
    // Send target brightness as 'brightness' in state
    extern TransitionEngine transition;
    doc["brightness"] = transition.getTargetBrightness();
    doc["effect"] = state.effect;
    doc["transitionTime"] = state.transitionTime;
    doc["currentPreset"] = state.currentPreset;
    if (_scheduler->isTimeValid()) {
        doc["time"] = _scheduler->getCurrentTime();
    } else {
        doc["time"] = "--:--";
    }
    doc["sunrise"] = _scheduler->getSunriseTime();
    doc["sunset"] = _scheduler->getSunsetTime();

    JsonObject paramsObj = doc.createNestedObject("params");
    paramsObj["speed"] = state.params.speed;
    paramsObj["intensity"] = state.params.intensity;
    JsonArray colorsArr = paramsObj.createNestedArray("colors");
    for (const auto& c : state.params.colors) {
        colorsArr.add(c);
    }

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

String WebServerManager::getConfigJSON() {
    StaticJsonDocument<4096> doc;
    JsonObject ledObj = doc.createNestedObject("led");
    ledObj["pin"] = _config->led.pin;
    ledObj["count"] = _config->led.count;
    ledObj["type"] = _config->led.type;
    ledObj["colorOrder"] = _config->led.colorOrder;
    ledObj["relayPin"] = _config->led.relayPin;
    ledObj["relayActiveHigh"] = _config->led.relayActiveHigh;

    JsonObject safetyObj = doc.createNestedObject("safety");
    safetyObj["minTransitionTime"] = _config->safety.minTransitionTime;
    // maxBrightness is now always stored as percent
    safetyObj["maxBrightness"] = _config->safety.maxBrightness;
    // Store maxBrightness as percent everywhere
    if (doc.containsKey("safety")) {
        JsonObject safetyObj = doc["safety"];
        if (safetyObj.containsKey("maxBrightness")) {
            int percent = safetyObj["maxBrightness"].as<int>();
            if (percent < 1) percent = 1;
            if (percent > 100) percent = 100;
            _config->safety.maxBrightness = percent;
            safetyObj["maxBrightness"] = percent; // update doc for file save
        }
    }

    JsonObject timeObj = doc.createNestedObject("time");
    timeObj["ntpServer"] = _config->time.ntpServer;
    timeObj["timezone"] = _config->time.timezone;
    timeObj["latitude"] = _config->time.latitude;
    timeObj["longitude"] = _config->time.longitude;
    timeObj["dstEnabled"] = _config->time.dstEnabled;

    // Add network fields
    JsonObject netObj = doc.createNestedObject("network");
    netObj["hostname"] = _config->network.hostname;
    netObj["apPassword"] = _config->network.apPassword;
    netObj["ssid"] = _config->network.ssid;

    // Add timers array to config JSON (not as 'schedule', but as 'timers' for consistency with upload)
    JsonArray timersArray = doc.createNestedArray("timers");
    size_t timerCount = 0;
    // Determine timer count from config object (timers loaded from config.json)
    for (size_t i = 0; i < _config->timers.size(); i++) {
        if (_config->timers[i].enabled || _config->timers[i].hour != 0 || _config->timers[i].minute != 0) {
            timerCount++;
        }
    }
    if (timerCount == 0) timerCount = _config->timers.size();
    for (size_t i = 0; i < _config->timers.size(); i++) {
        const auto& t = _config->timers[i];
        JsonObject timerObj = timersArray.createNestedObject();
        timerObj["id"] = i;
        timerObj["enabled"] = t.enabled;
        timerObj["type"] = t.type;
        timerObj["hour"] = t.hour;
        timerObj["minute"] = t.minute;
        timerObj["presetId"] = t.presetId;
        timerObj["brightness"] = t.brightness;
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
        timerObj["brightness"] = t.brightness;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

bool WebServerManager::applySafetyLimits(uint8_t& brightness, uint32_t& transitionTime) {
    bool modified = false;

    // maxBrightness is now percent, and brightness is 0-100, so clamp directly
    if (brightness > _config->safety.maxBrightness) {
        brightness = _config->safety.maxBrightness;
        modified = true;
    }

    if (transitionTime < _config->safety.minTransitionTime) {
        transitionTime = _config->safety.minTransitionTime;
        modified = true;
    }
    return modified;
}

void WebServerManager::broadcastState() {
    // Sync config.state.brightness with transition engine before broadcasting
    extern TransitionEngine transition;
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
