// ============================================================
// webserver.cpp – ESP-IDF HTTP server (esp_http_server)
// Replaces ESPAsyncWebServer
// ============================================================
#include "config.h"
#include "effects.h"
#include "network.h"
#include "ota.h"
#include "presets.h"
#include "state.h"
#include "transition.h"
#include "webserver.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cJSON.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "assets.h"

static const char *TAG = "webserver";

#define WEBSERVER_HEALTH_CHECK_INTERVAL_MS 30000
#define WEBSERVER_HEALTH_FAILS_BEFORE_RECOVER 3

extern TransitionEngine transition;
extern SystemState state;
extern TransitionEngine::PendingTransitionState pendingTransition;
extern volatile bool otaAckReceived;
extern volatile bool otaInProgress;

// Global pointer used by ota.cpp (defined in main.cpp)
extern WebServerManager *webServerPtr;

// Cached effects JSON
static std::string cachedEffectsJson;
static bool effectsCacheReady = false;

static void health_probe_work(void *arg) {
  (void)arg;
}

void WebServerManager::liveBinaryBroadcastWork(void *arg) {
  WebServerManager *mgr = static_cast<WebServerManager *>(arg);
  if (!mgr || !mgr->_liveFrameMutex) {
    return;
  }

  while (true) {
    std::vector<uint8_t> frame;

    if (xSemaphoreTake(mgr->_liveFrameMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
      return;
    }

    if (!mgr->_liveFrameDirty || mgr->_pendingLiveFrame.empty()) {
      mgr->_liveFrameQueued = false;
      xSemaphoreGive(mgr->_liveFrameMutex);
      return;
    }

    frame = mgr->_pendingLiveFrame;
    mgr->_liveFrameDirty = false;
    xSemaphoreGive(mgr->_liveFrameMutex);

    mgr->broadcastBinary(frame.data(), frame.size());
  }
}

#define LIVE_LED_BROADCAST_INTERVAL_MS 400

// ── CORS helper
// ───────────────────────────────────────────────────────────────
static void setCors(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// ── Read full request body
// ────────────────────────────────────────────────────
static std::string readBody(httpd_req_t *req) {
  if (req->content_len == 0)
    return "";
  size_t len = req->content_len;
  std::string body(len, '\0');
  int received = httpd_req_recv(req, &body[0], len);
  if (received < 0)
    return "";
  body.resize((size_t)received);
  return body;
}

// ── URL decode
// ────────────────────────────────────────────────────────────────
static std::string urlDecode(const std::string &input) {
  std::string decoded;
  char temp[3] = {0};
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '%' && i + 2 < input.size()) {
      temp[0] = input[i + 1];
      temp[1] = input[i + 2];
      decoded += (char)strtol(temp, NULL, 16);
      i += 2;
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

// ── Parse URL-encoded form param
// ───────────────────────────────────────────────
static std::string formParam(const std::string &body, const std::string &key) {
  std::string prefix = key + "=";
  size_t pos = body.find(prefix);
  if (pos == std::string::npos)
    return "";
  pos += prefix.size();
  size_t end = body.find('&', pos);
  std::string val = (end == std::string::npos) ? body.substr(pos)
                                               : body.substr(pos, end - pos);
  return urlDecode(val);
}

static cJSON *parseJsonBody(const std::string &body) {
  if (body.empty())
    return nullptr;
  return cJSON_Parse(body.c_str());
}

static int jsonIntOr(const cJSON *obj, const char *key, int fallback) {
  if (!cJSON_IsObject(obj))
    return fallback;
  const cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
  if (cJSON_IsNumber(item))
    return item->valueint;
  if (cJSON_IsBool(item))
    return cJSON_IsTrue(item) ? 1 : 0;
  return fallback;
}

static bool jsonBoolOr(const cJSON *obj, const char *key, bool fallback) {
  if (!cJSON_IsObject(obj))
    return fallback;
  const cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
  if (cJSON_IsBool(item))
    return cJSON_IsTrue(item);
  if (cJSON_IsNumber(item))
    return item->valueint != 0;
  return fallback;
}

static const char *jsonStringOr(const cJSON *obj, const char *key,
                                const char *fallback) {
  if (!cJSON_IsObject(obj))
    return fallback;
  const cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
  if (cJSON_IsString(item) && item->valuestring)
    return item->valuestring;
  return fallback;
}

// ── LiveLED timer callback (ISR-safe, just set flag) ─────────────────────────
void WebServerManager::liveLedTimerCb(void *arg) {
  WebServerManager *mgr = (WebServerManager *)arg;
  mgr->_liveLedTick = true;
}

// ── Effects cache
// ─────────────────────────────────────────────────────────────
void WebServerManager::buildEffectsCache() {
  cJSON *doc = cJSON_CreateObject();
  cJSON *effects = cJSON_CreateArray();
  cJSON_AddItemToObject(doc, "effects", effects);
  for (size_t i = 0; i < effectRegistry.size(); ++i) {
    cJSON *eff = cJSON_CreateObject();
    cJSON_AddItemToArray(effects, eff);
    cJSON_AddNumberToObject(eff, "id", effectRegistry[i].id);
    cJSON_AddStringToObject(eff, "name", effectRegistry[i].name);
  }
  cachedEffectsJson.clear();
  char *printed = cJSON_PrintUnformatted(doc);
  if (printed) {
    cachedEffectsJson = printed;
    cJSON_free(printed);
  }
  cJSON_Delete(doc);
  effectsCacheReady = true;
}

// ── Constructor
// ───────────────────────────────────────────────────────────────
WebServerManager::WebServerManager(Configuration *config,
                                   Scheduler *scheduler) {
  _config = config;
  _scheduler = scheduler;
  _liveFrameMutex = xSemaphoreCreateMutex();
}

// ── begin
// ─────────────────────────────────────────────────────────────────────
bool WebServerManager::startHttpServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 48;
  cfg.max_open_sockets = 5; // Keep HTTP server footprint low so OTA/TLS and
                            // other subsystems can always create sockets.
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 5;
  cfg.lru_purge_enable = true;
  cfg.stack_size =
      24576; // Large enough for JSON handlers + gz OTA decompression

  if (httpd_start(&_server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    _server = nullptr;
    return false;
  }

  setupWebSocket();
  setupRoutes();
  buildEffectsCache();

  _lastHealthCheckMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  _healthProbeFailures = 0;

  return true;
}

void WebServerManager::begin() {
  if (!startHttpServer()) {
    return;
  }

  // Live LED broadcast timer
  esp_timer_create_args_t ta = {};
  ta.callback = liveLedTimerCb;
  ta.arg = this;
  ta.name = "liveled";
  esp_timer_create(&ta, &_liveLedTimer);
  esp_timer_start_periodic(_liveLedTimer,
                           LIVE_LED_BROADCAST_INTERVAL_MS * 1000ULL);

  ESP_LOGI(TAG, "Web server started");
}

void WebServerManager::recoverHttpServer() {
  ESP_LOGW(TAG, "Recovering HTTP server after health probe failures");

  if (_server) {
    esp_err_t stopRc = httpd_stop(_server);
    if (stopRc != ESP_OK) {
      ESP_LOGW(TAG, "httpd_stop returned %d during recovery", stopRc);
    }
    _server = nullptr;
  }

  _wsHandshaked.clear();
  _otaClients.clear();

  if (startHttpServer()) {
    ESP_LOGI(TAG, "HTTP server recovered successfully");
  } else {
    ESP_LOGE(TAG, "HTTP server recovery failed");
  }
}

void WebServerManager::runHealthCheck() {
  if (!_server || otaInProgress) {
    return;
  }

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if ((now - _lastHealthCheckMs) < WEBSERVER_HEALTH_CHECK_INTERVAL_MS) {
    return;
  }
  _lastHealthCheckMs = now;

  esp_err_t rc = httpd_queue_work(_server, health_probe_work, nullptr);
  if (rc == ESP_OK) {
    if (_healthProbeFailures > 0) {
      ESP_LOGI(TAG, "HTTP health probe recovered");
    }
    _healthProbeFailures = 0;
    return;
  }

  _healthProbeFailures++;
  ESP_LOGW(TAG,
           "HTTP health probe failed rc=%d (consecutive=%u)",
           rc,
           (unsigned)_healthProbeFailures);

  if (_healthProbeFailures >= WEBSERVER_HEALTH_FAILS_BEFORE_RECOVER) {
    recoverHttpServer();
  }
}

// ── update (called from main_task)
// ────────────────────────────────────────────
void WebServerManager::update() {
  runHealthCheck();

  if (!_liveLedTick)
    return;
  _liveLedTick = false;

  if (otaInProgress)
    return;

  const uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  const bool powerTransitionActive =
      transition.isTransitioning() &&
      (transition._startState.brightness == 0 ||
       transition._targetState.brightness == 0);
  if (powerTransitionActive) {
    // Keep live stream available during long power transitions, but throttle
    // aggressively to reduce pressure on HTTP/WS handling.
    if ((nowMs - _lastBroadcast) < 1000U) {
      return;
    }
  }

  uint16_t n = _config->led.count;
  if (n == 0)
    return;

  const std::vector<uint32_t> *src =
      (g_outputFramePtr && g_outputFramePtr->size() >= n) ? g_outputFramePtr
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

  if (!_server || !_liveFrameMutex)
    return;

  bool shouldQueueWork = false;
  if (xSemaphoreTake(_liveFrameMutex, 0) != pdTRUE) {
    return;
  }

  _pendingLiveFrame.swap(buf);
  _liveFrameDirty = true;
  if (!_liveFrameQueued) {
    _liveFrameQueued = true;
    shouldQueueWork = true;
  }
  xSemaphoreGive(_liveFrameMutex);

  if (shouldQueueWork) {
    _lastBroadcast = nowMs;
    esp_err_t rc = httpd_queue_work(_server, liveBinaryBroadcastWork, this);
    if (rc != ESP_OK) {
      if (xSemaphoreTake(_liveFrameMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        _liveFrameQueued = false;
        xSemaphoreGive(_liveFrameMutex);
      }
      ESP_LOGW(TAG, "Failed to queue live WS work rc=%d", rc);
    }
  }
}

// ── WebSocket broadcast helpers
// ───────────────────────────────────────────────
void WebServerManager::cleanupDisconnectedClients() {
  if (!_server)
    return;
  size_t n = 16;
  int fds[16];
  httpd_get_client_list(_server, &n, fds);
  std::set<int> active(fds, fds + n);

  std::set<int> toErase;
  for (int fd : _otaClients)
    if (!active.count(fd))
      toErase.insert(fd);
  for (int fd : toErase)
    _otaClients.erase(fd);
  toErase.clear();
  for (auto &kv : _wsHandshaked)
    if (!active.count(kv.first))
      toErase.insert(kv.first);
  for (int fd : toErase)
    _wsHandshaked.erase(fd);

  toErase.clear();
  for (int fd : _wsBlocked)
    if (!active.count(fd))
      toErase.insert(fd);
  for (int fd : toErase)
    _wsBlocked.erase(fd);

  toErase.clear();
  for (auto &kv : _wsSendFailStreak)
    if (!active.count(kv.first))
      toErase.insert(kv.first);
  for (int fd : toErase)
    _wsSendFailStreak.erase(fd);
}

void WebServerManager::broadcastText(const std::string &msg,
                                     bool otaClientsOnly) {
  if (!_server)
    return;
  cleanupDisconnectedClients();
  size_t n = 16;
  int fds[16];
  httpd_get_client_list(_server, &n, fds);
  std::set<int> failedWsClients;
  for (size_t i = 0; i < n; i++) {
    if (httpd_ws_get_fd_info(_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
      continue;
    if (_wsBlocked.count(fds[i]))
      continue;
    bool isOta = _otaClients.count(fds[i]) > 0;
    if (otaClientsOnly && !isOta)
      continue;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)msg.c_str();
    frame.len = msg.size();
    esp_err_t rc = httpd_ws_send_frame_async(_server, fds[i], &frame);
    if (rc != ESP_OK) {
      failedWsClients.insert(fds[i]);
    } else {
      _wsSendFailStreak[fds[i]] = 0;
    }
  }
  for (int fd : failedWsClients) {
    uint8_t streak = _wsSendFailStreak[fd];
    if (streak < 255)
      ++streak;
    _wsSendFailStreak[fd] = streak;
    if (streak >= 3) {
      const bool firstBlock = _wsBlocked.insert(fd).second;
      _otaClients.erase(fd);
      _wsHandshaked.erase(fd);
      _wsSendFailStreak.erase(fd);
      if (firstBlock) {
        ESP_LOGW(TAG, "Dropping WS client fd=%d after %u send failures", fd,
                 (unsigned)streak);
        httpd_sess_trigger_close(_server, fd);
      }
    }
  }
}

void WebServerManager::broadcastBinary(const uint8_t *data, size_t len) {
  if (!_server)
    return;
  static uint32_t lastDropLogMs = 0;
  static uint32_t droppedSinceLastLog = 0;
  cleanupDisconnectedClients();
  size_t n = 16;
  int fds[16];
  httpd_get_client_list(_server, &n, fds);
  for (size_t i = 0; i < n; i++) {
    if (httpd_ws_get_fd_info(_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
      continue;
    if (_wsBlocked.count(fds[i]))
      continue;
    bool isOta = _otaClients.count(fds[i]) > 0;
    if (isOta)
      continue; // skip OTA clients for live LED data
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = (uint8_t *)data;
    frame.len = len;
    esp_err_t rc = httpd_ws_send_frame_async(_server, fds[i], &frame);
    if (rc == ESP_OK) {
      _wsSendFailStreak[fds[i]] = 0;
    } else {
      // Live strip stream is best-effort realtime: drop this frame for this
      // client and continue. Do not close the socket due to transient misses.
      droppedSinceLastLog++;
      const uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if ((nowMs - lastDropLogMs) >= 2000U) {
        ESP_LOGD(TAG, "Dropped %u live binary frames in last %ums (latest fd=%d rc=%d)",
                 (unsigned)droppedSinceLastLog,
                 (unsigned)(nowMs - lastDropLogMs),
                 fds[i],
                 rc);
        droppedSinceLastLog = 0;
        lastDropLogMs = nowMs;
      }
    }
  }
}

void WebServerManager::broadcastState() {
  broadcastText(getStateJSON(), false);
}

void WebServerManager::broadcastOtaStatus(const std::string &status,
                                          const std::string &message,
                                          int progress) {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "ota_status");
  cJSON_AddStringToObject(doc, "status", status.c_str());
  if (!message.empty())
    cJSON_AddStringToObject(doc, "message", message.c_str());
  if (progress >= 0)
    cJSON_AddNumberToObject(doc, "progress", progress);
  char *printed = cJSON_PrintUnformatted(doc);
  std::string json = printed ? printed : "{}";
  if (printed)
    cJSON_free(printed);
  cJSON_Delete(doc);
  broadcastText(json, true); // only OTA clients
}

// ── WebSocket handler
// ─────────────────────────────────────────────────────────
esp_err_t WebServerManager::wsHandler(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  int fd = httpd_req_to_sockfd(req);

  if (req->method == HTTP_GET) {
    // New WS connection – initialize tracking
    ESP_LOGI(TAG, "WS connect fd=%d", fd);
    mgr->_wsBlocked.erase(fd);
    mgr->_wsHandshaked[fd] = false;
    mgr->_otaClients.erase(fd);
    return ESP_OK;
  }

  httpd_ws_frame_t pkt = {};
  pkt.type = HTTPD_WS_TYPE_TEXT;

  // First pass: get length
  esp_err_t err = httpd_ws_recv_frame(req, &pkt, 0);
  if (err != ESP_OK || pkt.len == 0)
    return err;

  std::vector<uint8_t> buf(pkt.len + 1, 0);
  pkt.payload = buf.data();
  err = httpd_ws_recv_frame(req, &pkt, pkt.len);
  if (err != ESP_OK)
    return err;

  std::string msg((const char *)buf.data(), pkt.len);

  bool &handshaked = mgr->_wsHandshaked[fd];
  if (!handshaked) {
    handshaked = true;
    if (msg.find("\"type\":\"ota_client\"") != std::string::npos) {
      mgr->_otaClients.insert(fd);
    } else {
      mgr->_otaClients.erase(fd);
    }
    // Send state to new client
    std::string stateJson = mgr->getStateJSON();
    ESP_LOGD(TAG, "WS send state to fd=%d (%u bytes)", fd,
         (unsigned)stateJson.size());
    httpd_ws_frame_t resp = {};
    resp.type = HTTPD_WS_TYPE_TEXT;
    resp.payload = (uint8_t *)stateJson.c_str();
    resp.len = stateJson.size();
    httpd_ws_send_frame(req, &resp);
    return ESP_OK;
  }

  // Subsequent messages
  if (msg.find("\"type\":\"ping\"") != std::string::npos) {
    httpd_ws_frame_t resp = {};
    static const char pong[] = "{\"type\":\"pong\"}";
    resp.type = HTTPD_WS_TYPE_TEXT;
    resp.payload = (uint8_t *)pong;
    resp.len = sizeof(pong) - 1;
    httpd_ws_send_frame(req, &resp);
  } else if (msg.find("\"type\":\"ota_client\"") != std::string::npos) {
    mgr->_otaClients.insert(fd);
  } else if (msg.find("\"type\":\"state\"") != std::string::npos) {
    mgr->_otaClients.erase(fd);
    std::string stateJson = mgr->getStateJSON();
    ESP_LOGD(TAG, "WS send state (on request) to fd=%d (%u bytes)", fd,
             (unsigned)stateJson.size());
    httpd_ws_frame_t resp = {};
    resp.type = HTTPD_WS_TYPE_TEXT;
    resp.payload = (uint8_t *)stateJson.c_str();
    resp.len = stateJson.size();
    httpd_ws_send_frame(req, &resp);
  } else if (msg.find("\"type\":\"ota_ack\"") != std::string::npos) {
    otaAckReceived = true;
  }
  return ESP_OK;
}

// ── Route registrations
// ───────────────────────────────────────────────────────
void WebServerManager::setupWebSocket() {
  httpd_uri_t ws_uri = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = wsHandler,
      .user_ctx = this,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = nullptr,
  };
  httpd_register_uri_handler(_server, &ws_uri);
}

void WebServerManager::setupRoutes() {
#define URI(path, meth, fn)                                                    \
  {                                                                            \
    httpd_uri_t _u = {};                                                       \
    _u.uri = path;                                                             \
    _u.method = meth;                                                          \
    _u.handler = fn;                                                           \
    _u.user_ctx = this;                                                        \
    _u.is_websocket = false;                                                   \
    _u.handle_ws_control_frames = false;                                       \
    _u.supported_subprotocol = nullptr;                                        \
    httpd_register_uri_handler(_server, &_u);                                  \
  }

  URI("/api/version", HTTP_GET, hVersion)
  URI("/api/update", HTTP_GET, hUpdateGet)
  URI("/api/update", HTTP_POST, hUpdatePost)
  URI("/api/command", HTTP_POST, hCommand)
  URI("/api/command", HTTP_OPTIONS, hOptions)
  URI("/ota", HTTP_POST, hOtaUpload)
  URI("/ota", HTTP_OPTIONS, hOptions)
  URI("/generate_204", HTTP_GET, hCaptive)
  URI("/hotspot-detect.html", HTTP_GET, hCaptive)
  URI("/ncsi.txt", HTTP_GET, hCaptive)
  URI("/connecttest.txt", HTTP_GET, hCaptive)
  URI("/favicon.ico", HTTP_GET, hNoContent)
  URI("/wpad.dat", HTTP_GET, hNoContent)
  URI("/index.js", HTTP_GET, hIndexJs)
  URI("/app.js", HTTP_GET, hIndexJs)
  URI("/style.css", HTTP_GET, hStyleCss)
  URI("/", HTTP_GET, hRoot)
  URI("/index.html", HTTP_GET, hRoot)
  URI("/config.html", HTTP_GET, hRoot)
  URI("/wifi", HTTP_GET, hWifiGet)
  URI("/wifi/scan", HTTP_GET, hWifiScan)
  URI("/wifi", HTTP_POST, hWifiPost)
  URI("/api/state", HTTP_GET, hStateGet)
  URI("/api/state", HTTP_POST, hStatePost)
  URI("/api/state", HTTP_OPTIONS, hOptions)
  URI("/api/effects", HTTP_GET, hEffects)
  URI("/api/presets", HTTP_GET, hPresetsGet)
  URI("/api/presets", HTTP_OPTIONS, hOptions)
  URI("/api/preset", HTTP_POST, hPresetPost)
  URI("/api/preset", HTTP_OPTIONS, hOptions)
  URI("/api/config", HTTP_GET, hConfigGet)
  URI("/api/config", HTTP_POST, hConfigPost)
  URI("/api/config", HTTP_OPTIONS, hOptions)
  URI("/api/factory_reset", HTTP_POST, hFactoryReset)
  URI("/api/timer", HTTP_POST, hTimerPost)
  URI("/api/timer", HTTP_OPTIONS, hOptions)
  URI("/api/timezones", HTTP_GET, hTimezones)

  httpd_register_err_handler(_server, HTTPD_404_NOT_FOUND, hNotFound);
#undef URI
}

// ── Static route implementations ─────────────────────────────────────────────

// OPTIONS preflight
esp_err_t WebServerManager::hOptions(httpd_req_t *req) {
  ESP_LOGI(TAG, "hOptions called: %s", req->uri);
  setCors(req);
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// 404
esp_err_t WebServerManager::hNotFound(httpd_req_t *req, httpd_err_code_t err) {
  ESP_LOGW(TAG, "hNotFound called: %s", req->uri);
  httpd_resp_set_status(req, "404 Not Found");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "Not Found");
  return ESP_OK;
}

// /api/version
esp_err_t WebServerManager::hVersion(httpd_req_t *req) {
  ESP_LOGI(TAG, "hVersion called: %s", req->uri);
  setCors(req);
  httpd_resp_set_type(req, "application/json");
  std::string json = std::string("{\"version\":\"") + (const char *)version + "\"}";
  httpd_resp_sendstr(req, json.c_str());
  return ESP_OK;
}

// /api/update GET → return current firmware version and OTA environment.
// The browser uses this to know what it's running, then fetches the GitHub
// /api/update GET → fetch manifest from GitHub and return it with current
// version.
esp_err_t WebServerManager::hUpdateGet(httpd_req_t *req) {
  ESP_LOGI(TAG, "hUpdateGet called: %s", req->uri);
  setCors(req);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_type(req, "application/json");

  std::string manifest = fetchRemoteManifestJson();
  if (manifest.empty()) {
    httpd_resp_set_status(req, "502 Bad Gateway");
    httpd_resp_sendstr(req, "{\"error\":\"Could not reach GitHub\"}");
    return ESP_OK;
  }
  // Return {"current":"x.x.x","latest":[...manifest array...]}
  std::string json = std::string("{\"current\":\"") + (const char *)version +
                     ",\"latest\":" + manifest + "}";
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

// /api/update POST → start remote OTA (device resolves everything itself).
esp_err_t WebServerManager::hUpdatePost(httpd_req_t *req) {
  ESP_LOGI(TAG, "hUpdatePost called: %s", req->uri);
  setCors(req);
  if (otaInProgress) {
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(
        req, "{\"success\":false,\"message\":\"OTA already in progress\"}");
    return ESP_OK;
  }
#if CONFIG_FREERTOS_UNICORE
  const BaseType_t otaCore = 0;
#else
  const BaseType_t otaCore = 1;
#endif
  xTaskCreatePinnedToCore(otaTask, "otaTask", 24576, nullptr, 6, nullptr,
                          otaCore);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"OTA started\"}");
  return ESP_OK;
}

// /api/command POST
esp_err_t WebServerManager::hCommand(httpd_req_t *req) {
  ESP_LOGI(TAG, "hCommand called: %s", req->uri);
  setCors(req);
  std::string body = readBody(req);
  bool doReboot = false;
  if (!body.empty()) {
    cJSON *doc = parseJsonBody(body);
    if (doc) {
      std::string cmd = jsonStringOr(doc, "command", "");
      ESP_LOGI(TAG, "hCommand body: %s", body.c_str());
      if (cmd == "reboot")
        doReboot = true;
      cJSON_Delete(doc);
    }
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"OK\"}");
  if (doReboot) {
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  }
  return ESP_OK;
}

// /ota POST – local firmware upload
esp_err_t WebServerManager::hOtaUpload(httpd_req_t *req) {
  ESP_LOGI(TAG, "hOtaUpload called: %s", req->uri);
  return handleOtaUpload(req);
}

// Captive portal redirects
esp_err_t WebServerManager::hCaptive(httpd_req_t *req) {
  ESP_LOGI(TAG, "hCaptive called: %s", req->uri);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/wifi");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// 204 No Content (favicon, wpad, etc.)
esp_err_t WebServerManager::hNoContent(httpd_req_t *req) {
  ESP_LOGI(TAG, "hNoContent called: %s", req->uri);
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// /index.js
esp_err_t WebServerManager::hIndexJs(httpd_req_t *req) {
  ESP_LOGI(TAG, "hIndexJs called: %s", req->uri);
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_send(req, (const char *)web_index_js, INDEX_JS_SIZE);
  return ESP_OK;
}

// /style.css
esp_err_t WebServerManager::hStyleCss(httpd_req_t *req) {
  ESP_LOGI(TAG, "hStyleCss called: %s", req->uri);
  httpd_resp_set_type(req, "text/css");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_send(req, (const char *)web_style_css, STYLE_CSS_SIZE);
  return ESP_OK;
}

// / and /index.html
esp_err_t WebServerManager::hRoot(httpd_req_t *req) {
  ESP_LOGI(TAG, "hRoot called: %s", req->uri);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, (const char *)web_index_html, INDEX_HTML_SIZE);
  return ESP_OK;
}

// /wifi GET
esp_err_t WebServerManager::hWifiGet(httpd_req_t *req) {
  ESP_LOGI(TAG, "hWifiGet called: %s", req->uri);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, (const char *)web_index_html, INDEX_HTML_SIZE);
  return ESP_OK;
}

// /wifi/scan GET  – uses blocking scan (httpd handler has own task stack)
esp_err_t WebServerManager::hWifiScan(httpd_req_t *req) {
  ESP_LOGI(TAG, "hWifiScan called: %s", req->uri);
  setCors(req);
  httpd_resp_set_type(req, "application/json");

  wifi_mode_t mode = WIFI_MODE_NULL;
  esp_wifi_get_mode(&mode);
  if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
    ESP_LOGI(TAG, "hWifiScan: WiFi mode is %d, switching to APSTA for scan",
             mode);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();
  } else {
    ESP_LOGI(TAG, "hWifiScan: WiFi mode is %s, proceeding with scan",
             mode == WIFI_MODE_STA ? "STA" : "APSTA");
  }
  wifi_scan_config_t sc = {};
  // blocking=true: waits up to ~2 s for scan to complete
  esp_err_t rc = esp_wifi_scan_start(&sc, true);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "hWifiScan: scan failed, rc=%d", rc);
    httpd_resp_sendstr(req, "{\"error\":\"scan failed\"}");
    return ESP_OK;
  }
  uint16_t num = 0;
  esp_wifi_scan_get_ap_num(&num);
  ESP_LOGI(TAG, "hWifiScan: found %d APs", num);
  if (num == 0) {
    httpd_resp_sendstr(req, "[]");
    return ESP_OK;
  }
  wifi_ap_record_t *recs =
      (wifi_ap_record_t *)malloc(num * sizeof(wifi_ap_record_t));
  if (!recs) {
    ESP_LOGE(TAG, "hWifiScan: malloc failed");
    httpd_resp_sendstr(req, "[]");
    return ESP_OK;
  }
  esp_wifi_scan_get_ap_records(&num, recs);
  std::string json = "[";
  for (uint16_t i = 0; i < num; i++) {
    if (i > 0)
      json += ",";
    json += "\"";
    // escape double quotes in SSID just in case
    for (int j = 0; recs[i].ssid[j] && j < 32; j++) {
      char ch = (char)recs[i].ssid[j];
      if (ch == '"')
        json += "\\\"";
      else
        json += ch;
    }
    json += "\"";
    ESP_LOGI(TAG, "hWifiScan: AP[%d] SSID=%s", i, recs[i].ssid);
  }
  json += "]";
  free(recs);
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

// /wifi POST
esp_err_t WebServerManager::hWifiPost(httpd_req_t *req) {
  ESP_LOGI(TAG, "hWifiPost called: %s", req->uri);
  WebServerManager *mgr = fromReq(req);
  std::string body = readBody(req);
  ESP_LOGI(TAG, "hWifiPost body: %s", body.c_str());
  std::string ssid = formParam(body, "ssid");
  std::string password = formParam(body, "password");
  if (!ssid.empty()) {
    ESP_LOGI(TAG, "hWifiPost: received SSID=%s", ssid.c_str());
    const char *html = "<html><body><h2>Connecting...</h2><p>Device will "
                       "reboot.</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html);
    // Move config update and reboot to a separate task
    std::string ssidCopy = ssid;
    std::string passwordCopy = password;
    Configuration *configPtr = mgr->_config;
    xTaskCreate(
        [](void *p) {
          auto tup = (std::tuple<Configuration *, std::string, std::string> *)p;
          Configuration *cfg = std::get<0>(*tup);
          std::string ssidVal = std::get<1>(*tup);
          std::string passVal = std::get<2>(*tup);
          cfg->network.ssid = ssidVal;
          cfg->network.password = passVal;
          cfg->save();
          vTaskDelay(pdMS_TO_TICKS(500));
          esp_restart();
          vTaskDelete(nullptr);
        },
        "wifiCfgTask", 8192,
        new std::tuple<Configuration *, std::string, std::string>(
            configPtr, ssidCopy, passwordCopy),
        1, nullptr);
    return ESP_OK;
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, (const char *)web_index_html, INDEX_HTML_SIZE);
  return ESP_OK;
}

// /api/state GET
esp_err_t WebServerManager::hStateGet(httpd_req_t *req) {
  ESP_LOGI(TAG, "hStateGet called: %s", req->uri);
  WebServerManager *mgr = fromReq(req);
  setCors(req);
  httpd_resp_set_type(req, "application/json");
  std::string json = mgr->getStateJSON();
  ESP_LOGI(TAG, "hStateGet response: %s", json.c_str());
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

// /api/state POST
esp_err_t WebServerManager::hStatePost(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  mgr->handleSetState(req);
  return ESP_OK;
}

// /api/effects GET
esp_err_t WebServerManager::hEffects(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  if (!effectsCacheReady)
    mgr->buildEffectsCache();
  setCors(req);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, cachedEffectsJson.c_str(), cachedEffectsJson.size());
  return ESP_OK;
}

// /api/presets GET
esp_err_t WebServerManager::hPresetsGet(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  setCors(req);
  httpd_resp_set_type(req, "application/json");
  std::string json = mgr->getPresetsJSON();
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

// /api/preset POST
esp_err_t WebServerManager::hPresetPost(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  mgr->handleSetPreset(req);
  return ESP_OK;
}

// /api/config GET
esp_err_t WebServerManager::hConfigGet(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  setCors(req);
  httpd_resp_set_type(req, "application/json");
  std::string json = mgr->_config->toJsonString();
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

// /api/config POST
esp_err_t WebServerManager::hConfigPost(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  mgr->handleSetConfig(req);
  return ESP_OK;
}

// /api/factory_reset POST
esp_err_t WebServerManager::hFactoryReset(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  setCors(req);
  bool ok = mgr->_config->factoryReset();
  httpd_resp_set_type(req, "application/json");
  if (ok) {
    httpd_resp_sendstr(
        req, "{\"success\":true,\"message\":\"Factory reset, rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  } else {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(
        req, "{\"success\":false,\"error\":\"Failed to delete config\"}");
  }
  return ESP_OK;
}

// /api/timer POST
esp_err_t WebServerManager::hTimerPost(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  mgr->handleSetTimer(req);
  return ESP_OK;
}

// /api/timezones GET
esp_err_t WebServerManager::hTimezones(httpd_req_t *req) {
  WebServerManager *mgr = fromReq(req);
  setCors(req);
  std::vector<std::string> tzList = mgr->_config->getSupportedTimezones();
  cJSON *arr = cJSON_CreateArray();
  for (const auto &tz : tzList)
    cJSON_AddItemToArray(arr, cJSON_CreateString(tz.c_str()));
  char *printed = cJSON_PrintUnformatted(arr);
  std::string json = printed ? printed : "[]";
  if (printed)
    cJSON_free(printed);
  cJSON_Delete(arr);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.size());
  return ESP_OK;
}

// ── Handler implementations
// ───────────────────────────────────────────────────

void WebServerManager::handleSetState(httpd_req_t *req) {
  setCors(req);
  std::string body = readBody(req);
  cJSON *doc = parseJsonBody(body);
  if (!doc) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  bool updated = false;
  bool callbackBroadcasted = false;
  const bool hasPower = cJSON_GetObjectItemCaseSensitive(doc, "power") != nullptr;
  const bool requestedPower = jsonBoolOr(doc, "power", state.power);

  // Apply power first to avoid brightness->power ordering artifacts that can
  // visually relight before a power-off transition.
  if (hasPower) {
    if (_powerCallback) {
      _powerCallback(requestedPower);
      callbackBroadcasted = true;
    }
    updated = true;
  }

  if (cJSON_GetObjectItemCaseSensitive(doc, "brightness")) {
    uint8_t brightness = percentToHex((uint8_t)jsonIntOr(doc, "brightness", 100));
    applyBrightnessLimit(brightness);
    applyTransitionTimeLimit(state.transitionTime);

    // If this payload is powering off, only store preferred brightness for next
    // power-on. Do not start a brightness transition while turning off.
    if (hasPower && !requestedPower) {
      state.brightness = brightness;
      updated = true;
    } else if (_brightnessCallback) {
      _brightnessCallback(brightness);
      callbackBroadcasted = true;
      updated = true;
    }
  }
  if (cJSON_GetObjectItemCaseSensitive(doc, "transitionTime")) {
    uint32_t t = (uint32_t)jsonIntOr(doc, "transitionTime", state.transitionTime);
    applyTransitionTimeLimit(t);
    state.transitionTime = t;
    updated = true;
  }
  if (cJSON_GetObjectItemCaseSensitive(doc, "effect")) {
    uint8_t effect = (uint8_t)jsonIntOr(doc, "effect", state.effect);
    if (_effectCallback)
      _effectCallback(effect, state.params);
    updated = true;
  }
  cJSON *paramsObj = cJSON_GetObjectItemCaseSensitive(doc, "params");
  if (cJSON_IsObject(paramsObj)) {
    EffectParams params = state.params;
    cJSON *speedItem = cJSON_GetObjectItemCaseSensitive(paramsObj, "speed");
    if (cJSON_IsNumber(speedItem)) {
      params.speed = percentToHex((uint8_t)speedItem->valueint);
      updated = true;
    }
    cJSON *intensityItem =
        cJSON_GetObjectItemCaseSensitive(paramsObj, "intensity");
    if (cJSON_IsNumber(intensityItem)) {
      params.intensity = percentToHex((uint8_t)intensityItem->valueint);
      updated = true;
    }
    cJSON *colorsArr = cJSON_GetObjectItemCaseSensitive(paramsObj, "colors");
    if (cJSON_IsArray(colorsArr)) {
      std::vector<std::string> parsedColors;
      cJSON *v = nullptr;
      cJSON_ArrayForEach(v, colorsArr) {
        if (cJSON_IsString(v) && v->valuestring) {
          std::string hex = v->valuestring;
          if (hex.size() == 6 && hex[0] != '#')
            hex = "#" + hex;
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
  if (updated && !callbackBroadcasted)
    broadcastState();

  cJSON_Delete(doc);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
}

void WebServerManager::handleGetPresets(httpd_req_t *req) {
  setCors(req);
  httpd_resp_set_type(req, "application/json");
  std::string json = getPresetsJSON();
  httpd_resp_send(req, json.c_str(), json.size());
}

void WebServerManager::handleSetPreset(httpd_req_t *req) {
  setCors(req);
  std::string body = readBody(req);
  cJSON *doc = parseJsonBody(body);
  if (!doc) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  if (!cJSON_GetObjectItemCaseSensitive(doc, "id")) {
    cJSON_Delete(doc);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Missing preset ID\"}");
    return;
  }
  int reqId = jsonIntOr(doc, "id", -1);
  auto it = std::find_if(_config->presets.begin(), _config->presets.end(),
                         [reqId](const Preset &p) { return p.id == reqId; });
  if (it == _config->presets.end()) {
    cJSON_Delete(doc);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid preset ID\"}");
    return;
  }
  if (jsonBoolOr(doc, "apply", false)) {
    if (_presetCallback)
      _presetCallback(it->id);
  } else {
    it->name = jsonStringOr(doc, "name", "");
    it->effect = (uint8_t)jsonIntOr(doc, "effect", it->effect);
    it->enabled = jsonBoolOr(doc, "enabled", true);
    cJSON *paramsObj = cJSON_GetObjectItemCaseSensitive(doc, "params");
    if (cJSON_IsObject(paramsObj)) {
      cJSON *speed = cJSON_GetObjectItemCaseSensitive(paramsObj, "speed");
      it->params.speed = cJSON_IsNumber(speed)
                             ? percentToHex((uint8_t)speed->valueint)
                             : percentToHex(100);
      cJSON *intensity =
          cJSON_GetObjectItemCaseSensitive(paramsObj, "intensity");
      it->params.intensity = cJSON_IsNumber(intensity)
                                 ? percentToHex((uint8_t)intensity->valueint)
                                 : percentToHex(50);
      it->params.colors.clear();
      cJSON *colorsArr = cJSON_GetObjectItemCaseSensitive(paramsObj, "colors");
      if (cJSON_IsArray(colorsArr)) {
        cJSON *v = nullptr;
        cJSON_ArrayForEach(v, colorsArr) {
          if (cJSON_IsString(v) && v->valuestring)
            it->params.colors.push_back(v->valuestring);
        }
      }
    }
    savePresets(_config->presets);
  }
  cJSON_Delete(doc);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
}

void WebServerManager::handleSetConfig(httpd_req_t *req) {
  setCors(req);
  std::string body = readBody(req);
  cJSON *doc = parseJsonBody(body);
  if (!doc) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  cJSON *netObj = cJSON_GetObjectItemCaseSensitive(doc, "network");
  if (cJSON_IsObject(netObj)) {
    cJSON *ssidNode = cJSON_GetObjectItemCaseSensitive(netObj, "ssid");
    const bool hasSsid = cJSON_IsString(ssidNode) && ssidNode->valuestring;
    const char *ssidVal = hasSsid ? ssidNode->valuestring : nullptr;
    ESP_LOGI(TAG, "Config POST network ssid present=%d len=%u", hasSsid ? 1 : 0,
             ssidVal ? (unsigned)strlen(ssidVal) : 0U);
    if (hasSsid && ssidVal && ssidVal[0] == '\0') {
      ESP_LOGW(TAG, "Config POST network ssid is empty");
    }
    cJSON *passwordNode = cJSON_GetObjectItemCaseSensitive(netObj, "password");
    if (hasSsid)
      _config->network.ssid = ssidVal;
    if (cJSON_IsString(passwordNode) && passwordNode->valuestring)
      _config->network.password = passwordNode->valuestring;
  }
  _config->partialUpdate(doc);
  bool ok = _config->save();
  cJSON_Delete(doc);
  httpd_resp_set_type(req, "application/json");
  if (ok) {
    if (_configCallback)
      _configCallback();
    httpd_resp_sendstr(req, "{\"success\":true}");
  } else {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Save failed\"}");
  }
}

void WebServerManager::handleSetTimer(httpd_req_t *req) {
  setCors(req);
  std::string body = readBody(req);
  cJSON *doc = parseJsonBody(body);
  if (!doc) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t timerId = (uint8_t)jsonIntOr(doc, "id", 255);
  if (timerId >= _config->timers.size()) {
    cJSON_Delete(doc);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid timer ID\"}");
    return;
  }
  _config->timers[timerId].enabled = jsonBoolOr(doc, "enabled", false);
  _config->timers[timerId].type =
      (TimerType)jsonIntOr(doc, "type", (int)TIMER_REGULAR);
  _config->timers[timerId].hour = (uint8_t)jsonIntOr(doc, "hour", 0);
  _config->timers[timerId].minute = (uint8_t)jsonIntOr(doc, "minute", 0);
  _config->timers[timerId].presetId = (uint8_t)jsonIntOr(doc, "presetId", 0);
  if (cJSON_GetObjectItemCaseSensitive(doc, "brightness"))
    _config->timers[timerId].brightness =
        percentToHex((uint8_t)jsonIntOr(doc, "brightness", 100));
  cJSON_Delete(doc);
  _config->save();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
}

// ── JSON generators
// ───────────────────────────────────────────────────────────

std::string WebServerManager::getStateJSON() {
  cJSON *doc = cJSON_CreateObject();
  bool inTrans = state.inTransition;
  if (inTrans) {
    cJSON_AddBoolToObject(doc, "power", state.power);
    cJSON_AddNumberToObject(doc, "effect", pendingTransition.effect);
    cJSON_AddNumberToObject(doc, "preset", pendingTransition.preset);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddItemToObject(doc, "params", p);
    cJSON_AddNumberToObject(p, "speed", hexToPercent(pendingTransition.params.speed));
    cJSON_AddNumberToObject(p, "intensity",
                            hexToPercent(pendingTransition.params.intensity));
    cJSON *ca = cJSON_CreateArray();
    cJSON_AddItemToObject(p, "colors", ca);
    for (const auto &c : pendingTransition.params.colors)
      cJSON_AddItemToArray(ca, cJSON_CreateString(c.c_str()));
  } else {
    cJSON_AddBoolToObject(doc, "power", state.power);
    cJSON_AddNumberToObject(doc, "effect", state.effect);
    cJSON_AddNumberToObject(doc, "preset", state.preset);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddItemToObject(doc, "params", p);
    cJSON_AddNumberToObject(p, "speed", hexToPercent(state.params.speed));
    cJSON_AddNumberToObject(p, "intensity", hexToPercent(state.params.intensity));
    cJSON *ca = cJSON_CreateArray();
    cJSON_AddItemToObject(p, "colors", ca);
    for (const auto &c : state.params.colors)
      cJSON_AddItemToArray(ca, cJSON_CreateString(c.c_str()));
  }
  int brightness = hexToPercent(transition._targetState.brightness);
  uint32_t transitionTime = state.transitionTime;
  bool timeValid = _scheduler->isTimeValid();
  std::string timeStr =
      timeValid ? _scheduler->getCurrentTime().c_str() : "--:--";
  std::string sunriseStr = _scheduler->getSunriseTime().c_str();
  std::string sunsetStr = _scheduler->getSunsetTime().c_str();
  cJSON_AddNumberToObject(doc, "brightness", brightness);
  cJSON_AddNumberToObject(doc, "transitionTime", transitionTime);
  cJSON_AddStringToObject(doc, "time", timeStr.c_str());
  cJSON_AddStringToObject(doc, "sunrise", sunriseStr.c_str());
  cJSON_AddStringToObject(doc, "sunset", sunsetStr.c_str());

  char *printed = cJSON_PrintUnformatted(doc);
  std::string out = printed ? printed : "{}";
  if (printed)
    cJSON_free(printed);
  cJSON_Delete(doc);
  return out;
}

std::string WebServerManager::getPresetsJSON() {
  cJSON *doc = cJSON_CreateObject();
  cJSON *arr = cJSON_CreateArray();
  cJSON_AddItemToObject(doc, "presets", arr);
  for (size_t i = 0; i < _config->getPresetCount(); i++) {
    if (_config->presets[i].name.empty() && i > 0)
      continue;
    const auto &preset = _config->presets[i];
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddItemToArray(arr, obj);
    cJSON_AddNumberToObject(obj, "id", (int)i);
    cJSON_AddStringToObject(obj, "name", preset.name.c_str());
    cJSON_AddNumberToObject(obj, "effect", preset.effect);
    cJSON_AddBoolToObject(obj, "enabled", preset.enabled);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, "params", p);
    cJSON_AddNumberToObject(p, "speed", preset.params.speed);
    cJSON_AddNumberToObject(p, "intensity", hexToPercent(preset.params.intensity));
    cJSON *ca = cJSON_CreateArray();
    cJSON_AddItemToObject(p, "colors", ca);
    for (const auto &c : preset.params.colors)
      cJSON_AddItemToArray(ca, cJSON_CreateString(c.c_str()));
  }
  char *printed = cJSON_PrintUnformatted(doc);
  std::string out = printed ? printed : "{}";
  if (printed)
    cJSON_free(printed);
  cJSON_Delete(doc);
  return out;
}

std::string WebServerManager::getTimersJSON() {
  cJSON *doc = cJSON_CreateObject();
  cJSON *arr = cJSON_CreateArray();
  cJSON_AddItemToObject(doc, "timers", arr);
  for (size_t i = 0; i < _config->timers.size(); i++) {
    const auto &t = _config->timers[i];
    if (!t.enabled && t.hour == 0 && t.minute == 0)
      continue;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddItemToArray(arr, obj);
    cJSON_AddNumberToObject(obj, "id", (int)i);
    cJSON_AddBoolToObject(obj, "enabled", t.enabled);
    cJSON_AddNumberToObject(obj, "type", (int)t.type);
    cJSON_AddNumberToObject(obj, "hour", t.hour);
    cJSON_AddNumberToObject(obj, "minute", t.minute);
    cJSON_AddNumberToObject(obj, "presetId", t.presetId);
    cJSON_AddNumberToObject(obj, "brightness", hexToPercent(t.brightness));
  }
  char *printed = cJSON_PrintUnformatted(doc);
  std::string out = printed ? printed : "{}";
  if (printed)
    cJSON_free(printed);
  cJSON_Delete(doc);
  return out;
}

// ── OTA client management
// ─────────────────────────────────────────────────────
void WebServerManager::clearOtaSubscriptions() { _otaClients.clear(); }

void WebServerManager::closeOtaClients() {
  if (!_server)
    return;
  for (int fd : _otaClients) {
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_CLOSE;
    httpd_ws_send_frame_async(_server, fd, &frame);
  }
  _otaClients.clear();
}

int WebServerManager::otaClientsConnected() const {
  if (!_server)
    return 0;
  size_t n = 16;
  int fds[16];
  httpd_get_client_list(_server, &n, fds);
  int count = 0;
  for (size_t i = 0; i < n; i++) {
    if (_otaClients.count(fds[i]))
      count++;
  }
  return count;
}

// ── Safety helpers
// ────────────────────────────────────────────────────────────
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

// ── Callback setters ─────────────────────────────────────────────────────────
void WebServerManager::onPowerChange(void (*cb)(bool)) { _powerCallback = cb; }
void WebServerManager::onBrightnessChange(void (*cb)(uint8_t)) {
  _brightnessCallback = cb;
}
void WebServerManager::onEffectChange(void (*cb)(uint8_t,
                                                 const EffectParams &)) {
  _effectCallback = cb;
}
void WebServerManager::onPresetApply(void (*cb)(uint8_t)) {
  _presetCallback = cb;
}
void WebServerManager::onConfigChange(void (*cb)()) { _configCallback = cb; }
