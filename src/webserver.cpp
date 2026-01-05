#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR)
#include <pgmspace.h>
#endif
#include "web_assets/index_html.inc"
#include "web_assets/wifi_html.inc"
#include "web_assets/app_js.inc"
#include "web_assets/style_css.inc"
#include "web_assets/fflate_min_js.inc"

#include "webserver.h"
#if defined(ESP32)
#include <Update.h>
#elif defined(ESP8266)
#include <Updater.h>
#endif
#include "debug.h"
#include "transition.h"


// Helper: CORS headers for API responses
static const char* CORS_HEADERS[][2] = {
    {"Access-Control-Allow-Origin", "*"},
    {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
    {"Access-Control-Allow-Headers", "Content-Type"}
};
static const size_t CORS_HEADER_COUNT = sizeof(CORS_HEADERS) / sizeof(CORS_HEADERS[0]);

extern TransitionEngine transition;

// Helper: URL decode for form fields (declaration)
static String urlDecode(const String& input);

// Place at the very end of the file, after all other code
void WebServerManager::handleOTAUpdate(AsyncWebServerRequest* request, unsigned char* data, unsigned int len, unsigned int index, unsigned int total) {
    // Actual OTA update logic
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
    if (index + len == total) {
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
    _server->begin();
    debugPrintln("Web server started");
}

void WebServerManager::update() {
    _ws->cleanupClients();
    // No periodic broadcast; state is sent only on connection and on actual changes
}

void WebServerManager::setupWebSocket() {
    _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                     AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            debugPrintln("WebSocket client connected");
            // Send current state immediately to the new client
            client->text(getStateJSON());
        } else if (type == WS_EVT_DISCONNECT) {
            debugPrintln("WebSocket client disconnected");
        }
    });
    _server->addHandler(_ws);
}

void WebServerManager::setupRoutes() {
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
        debugPrint(tag);
        debugPrint(": ");
        debugPrint(request->method());
        debugPrint(" ");
        debugPrintln(request->url());
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
            logRequest(request, "[DEBUG] /wifi POST (request handler)");
            debugPrint("[DEBUG] /wifi POST Content-Type: ");
            debugPrintln(request->contentType());
            debugPrint("[DEBUG] /wifi POST Headers: ");
            for (size_t i = 0; i < request->headers(); i++) {
                debugPrint("    ");
                debugPrint(request->headerName(i));
                debugPrint(": ");
                debugPrintln(request->header(i));
            }
            debugPrint("[DEBUG] /wifi POST Params: ");
            for (size_t i = 0; i < request->params(); i++) {
                debugPrint("    ");
                debugPrint(request->getParam(i)->name());
                debugPrint(": ");
                debugPrintln(request->getParam(i)->value());
            }
            // Fallback: If body handler is not called, parse POST params here
            if (request->hasParam("ssid", true)) {
                String ssid = urlDecode(request->getParam("ssid", true)->value());
                String password = request->hasParam("password", true) ? urlDecode(request->getParam("password", true)->value()) : "";
                debugPrint("[DEBUG] Fallback parsed ssid: "); debugPrintln(ssid);
                debugPrint("[DEBUG] Fallback parsed password: "); debugPrintln(password);
                if (ssid.length() > 0) {
                    _config->network.ssid = ssid;
                    _config->network.password = password;
                    debugPrintln("[DEBUG] Saving config after WiFi form submit (fallback)...");
                    _config->save();
                    debugPrintln("[DEBUG] Config saved. Rebooting...");
                    String html = "<html><body><h2>Connecting to WiFi...</h2><p>Device will reboot if successful.</p></body></html>";
                    request->send(200, "text/html", html);
                    delay(1000);
                    ESP.restart();
                    return;
                }
                debugPrintln("[DEBUG] SSID missing or POST parse failed (fallback), serving WiFi form again");
                request->send_P(200, "text/html", web_wifi_html, web_wifi_html_len);
            }
        }, 
        nullptr,
        [this, logRequest](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            debugPrintln("[DEBUG] /wifi POST body handler CALLED");
            logRequest(request, "[DEBUG] /wifi POST (body handler)");
            debugPrint("[DEBUG] /wifi POST body length: ");
            debugPrintln(len);
            String body;
            for (size_t i = 0; i < len; ++i) body += (char)data[i];
            debugPrint("[DEBUG] /wifi POST raw body: ");
            debugPrintln(body);
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
            debugPrint("[DEBUG] parsed ssid: "); debugPrintln(ssid);
            debugPrint("[DEBUG] parsed password: "); debugPrintln(password);
            if (ssid.length() > 0) {
                _config->network.ssid = ssid;
                _config->network.password = password;
                debugPrintln("[DEBUG] Saving config after WiFi form submit...");
                _config->save();
                debugPrintln("[DEBUG] Config saved. Rebooting...");
                String html = "<html><body><h2>Connecting to WiFi...</h2><p>Device will reboot if successful.</p></body></html>";
                request->send(200, "text/html", html);
                delay(1000);
                ESP.restart();
                return;
            }
            debugPrintln("[DEBUG] SSID missing or POST parse failed, serving WiFi form again");
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
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
        return;
    }
    // Merge incoming state with current state
    uint8_t brightness = _config->state.brightness;
    uint32_t transitionTime = _config->state.transitionTime;
    bool power = _config->state.power;
    uint8_t effect = _config->state.effect;
    EffectParams params = _config->state.params;

    if (doc.containsKey("brightness")) {
        brightness = doc["brightness"];
    }
    if (doc.containsKey("transitionTime")) {
        transitionTime = (uint32_t)doc["transitionTime"];
    }
    if (doc.containsKey("power")) {
        power = doc["power"];
    }
    if (doc.containsKey("effect")) {
        effect = (uint8_t)(int)doc["effect"];
    }
    if (doc.containsKey("params")) {
        JsonObject paramsObj = doc["params"];
        params.speed = paramsObj["speed"] | params.speed;
        params.intensity = paramsObj["intensity"] | params.intensity;
        params.color1 = paramsObj["color1"] | params.color1;
        params.color2 = paramsObj["color2"] | params.color2;
    }

    // Apply safety limits for brightness and/or transitionTime
    applySafetyLimits(brightness, transitionTime);

    // Update state and call callbacks
    if (_powerCallback) _powerCallback(power);
    if (_brightnessCallback) _brightnessCallback(brightness);
    if (_effectCallback) _effectCallback(effect, params);
    _config->state.transitionTime = transitionTime;

    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
    broadcastState();
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
    
    uint8_t presetId = doc["id"] | 0;
    
    if (presetId >= MAX_PRESETS) {
        {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid preset ID\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
        return;
    }
    
    // Apply or save preset
    if (doc.containsKey("apply") && doc["apply"]) {
        if (_presetCallback) _presetCallback(presetId);
    } else {
        // Save preset data
        _config->presets[presetId].name = doc["name"] | "";
        _config->presets[presetId].brightness = doc["brightness"] | 128;
        _config->presets[presetId].effect = (uint8_t)(int)doc["effect"];
        _config->presets[presetId].enabled = doc["enabled"] | true;
        
        if (doc.containsKey("params")) {
            JsonObject paramsObj = doc["params"];
            _config->presets[presetId].params.speed = paramsObj["speed"] | 128;
            _config->presets[presetId].params.intensity = paramsObj["intensity"] | 128;
            _config->presets[presetId].params.color1 = paramsObj["color1"] | 0x0000FF;
            _config->presets[presetId].params.color2 = paramsObj["color2"] | 0x00FFFF;
        }
        
        _config->savePresets();
    }
    
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
    broadcastState();
}

void WebServerManager::handleGetConfig(AsyncWebServerRequest* request) {
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", getConfigJSON());
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

void WebServerManager::handleSetConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    debugPrintln("[DEBUG] handleSetConfig called");
    String incomingJson;
    serializeJson(doc, incomingJson);
    debugPrint("[DEBUG] Incoming config JSON: ");
    debugPrintln(incomingJson);

    if (error) {
        debugPrintln("[DEBUG] JSON parse error in handleSetConfig");
        {
            AsyncWebServerResponse *resp = request->beginResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
            request->send(resp);
        }
        return;
    }

    // Update configuration
    if (doc.containsKey("led")) {
        JsonObject ledObj = doc["led"];
        if (ledObj.containsKey("count")) {
            _config->led.count = ledObj["count"];
        }
        if (ledObj.containsKey("type")) {
            _config->led.type = ledObj["type"] | _config->led.type;
        }
        if (ledObj.containsKey("relayPin")) {
            _config->led.relayPin = ledObj["relayPin"];
        }
        if (ledObj.containsKey("relayActiveHigh")) {
            _config->led.relayActiveHigh = ledObj["relayActiveHigh"];
        }
        // Note: Changing LED pin/type/relay config may require reboot for some changes
    }

    if (doc.containsKey("safety")) {
        JsonObject safetyObj = doc["safety"];
        _config->safety.minTransitionTime = safetyObj["minTransitionTime"] | _config->safety.minTransitionTime;
        _config->safety.maxBrightness = safetyObj["maxBrightness"] | _config->safety.maxBrightness;
    }

    if (doc.containsKey("time")) {
        JsonObject timeObj = doc["time"];
        _config->time.timezoneOffset = timeObj["timezoneOffset"] | _config->time.timezoneOffset;
        _config->time.latitude = timeObj["latitude"] | _config->time.latitude;
        _config->time.longitude = timeObj["longitude"] | _config->time.longitude;
        _config->time.dstEnabled = timeObj["dstEnabled"] | _config->time.dstEnabled;
    }

    bool saveResult = _config->save();
    debugPrint("[DEBUG] Config save result: ");
    debugPrintln(saveResult ? "success" : "FAIL");

    if (_configCallback) _configCallback();

    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
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
    
    if (timerId >= MAX_TIMERS + MAX_SUN_TIMERS) {
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
    _config->timers[timerId].days = doc["days"] | 0b1111111;
    _config->timers[timerId].offset = doc["offset"] | 0;
    _config->timers[timerId].presetId = doc["presetId"] | 0;
    
    _config->save();
    
    {
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        for (size_t i = 0; i < CORS_HEADER_COUNT; ++i) resp->addHeader(CORS_HEADERS[i][0], CORS_HEADERS[i][1]);
        request->send(resp);
    }
}

String WebServerManager::getStateJSON() {
    StaticJsonDocument<512> doc;
    
    doc["power"] = _config->state.power;
    // Send target brightness as 'brightness' in state
    extern TransitionEngine transition;
    doc["brightness"] = transition.getTargetBrightness();
    doc["effect"] = _config->state.effect;
    doc["transitionTime"] = _config->state.transitionTime;
    doc["currentPreset"] = _config->state.currentPreset;
    doc["time"] = _scheduler->getCurrentTime();
    doc["sunrise"] = _scheduler->getSunriseTime();
    doc["sunset"] = _scheduler->getSunsetTime();

    JsonObject paramsObj = doc.createNestedObject("params");
    paramsObj["speed"] = _config->state.params.speed;
    paramsObj["intensity"] = _config->state.params.intensity;
    paramsObj["color1"] = _config->state.params.color1;
    paramsObj["color2"] = _config->state.params.color2;

    String output;
    serializeJson(doc, output);
    return output;
}

String WebServerManager::getPresetsJSON() {
    StaticJsonDocument<4096> doc;
    JsonArray presetsArray = doc.createNestedArray("presets");
    
    for (int i = 0; i < MAX_PRESETS; i++) {
        if (_config->presets[i].name.length() == 0 && i > 0) continue;
        
        JsonObject presetObj = presetsArray.createNestedObject();
        presetObj["id"] = i;
        presetObj["name"] = _config->presets[i].name;
        presetObj["brightness"] = _config->presets[i].brightness;
        presetObj["effect"] = _config->presets[i].effect;
        presetObj["enabled"] = _config->presets[i].enabled;
        
        JsonObject paramsObj = presetObj.createNestedObject("params");
        paramsObj["speed"] = _config->presets[i].params.speed;
        paramsObj["intensity"] = _config->presets[i].params.intensity;
        paramsObj["color1"] = _config->presets[i].params.color1;
        paramsObj["color2"] = _config->presets[i].params.color2;
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

String WebServerManager::getConfigJSON() {
    StaticJsonDocument<1024> doc;
    
    JsonObject ledObj = doc.createNestedObject("led");
    ledObj["pin"] = _config->led.pin;
    ledObj["count"] = _config->led.count;
    ledObj["type"] = _config->led.type;
    ledObj["relayPin"] = _config->led.relayPin;
    ledObj["relayActiveHigh"] = _config->led.relayActiveHigh;
    
    JsonObject safetyObj = doc.createNestedObject("safety");
    safetyObj["minTransitionTime"] = _config->safety.minTransitionTime;
    safetyObj["maxBrightness"] = _config->safety.maxBrightness;
    
    JsonObject timeObj = doc.createNestedObject("time");
    timeObj["ntpServer"] = _config->time.ntpServer;
    timeObj["timezoneOffset"] = _config->time.timezoneOffset;
    timeObj["latitude"] = _config->time.latitude;
    timeObj["longitude"] = _config->time.longitude;
    timeObj["dstEnabled"] = _config->time.dstEnabled;
    
    String output;
    serializeJson(doc, output);
    return output;
}

String WebServerManager::getTimersJSON() {
    StaticJsonDocument<2048> doc;
    JsonArray timersArray = doc.createNestedArray("timers");
    
    for (int i = 0; i < MAX_TIMERS + MAX_SUN_TIMERS; i++) {
        JsonObject timerObj = timersArray.createNestedObject();
        timerObj["id"] = i;
        timerObj["enabled"] = _config->timers[i].enabled;
        timerObj["type"] = _config->timers[i].type;
        timerObj["hour"] = _config->timers[i].hour;
        timerObj["minute"] = _config->timers[i].minute;
        timerObj["days"] = _config->timers[i].days;
        timerObj["offset"] = _config->timers[i].offset;
        timerObj["presetId"] = _config->timers[i].presetId;
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

bool WebServerManager::applySafetyLimits(uint8_t& brightness, uint32_t& transitionTime) {
    bool modified = false;
    debugPrint("[DEBUG] applySafetyLimits: brightness in=" );
    debugPrint((int)brightness);
    debugPrint(", transitionTime in=");
    debugPrint((unsigned long)transitionTime);
    debugPrint(", minTransitionTime=");
    debugPrint((unsigned long)_config->safety.minTransitionTime);
    debugPrint(", maxBrightness=");
    debugPrintln((int)_config->safety.maxBrightness);

    if (brightness > _config->safety.maxBrightness) {
        brightness = _config->safety.maxBrightness;
        modified = true;
    }

    if (transitionTime < _config->safety.minTransitionTime) {
        transitionTime = _config->safety.minTransitionTime;
        modified = true;
    }

    debugPrint("[DEBUG] applySafetyLimits: brightness out=" );
    debugPrint((int)brightness);
    debugPrint(", transitionTime out=");
    debugPrintln((unsigned long)transitionTime);
    return modified;
}

void WebServerManager::broadcastState() {
    // Sync config.state.brightness with transition engine before broadcasting
    extern TransitionEngine transition;
    _config->state.brightness = transition.getCurrentBrightness();
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
