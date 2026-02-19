#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "config.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "scheduler.h"
#include <map>
#include <set>
#include <string>

class WebServerManager {
public:
  WebServerManager(Configuration *config, Scheduler *scheduler);

  void begin();
  void update();
  void broadcastState();

  // OTA status broadcast (std::string)
  void broadcastOtaStatus(const std::string &status,
                          const std::string &message = "", int progress = -1);

  // Callbacks for control actions
  void onPowerChange(void (*callback)(bool));
  void onBrightnessChange(void (*callback)(uint8_t));
  void onEffectChange(void (*callback)(uint8_t, const EffectParams &));
  void onPresetApply(void (*callback)(uint8_t));
  void onConfigChange(void (*callback)());

  // Safety helpers available for other modules
  bool applyBrightnessLimit(uint8_t &brightness);
  bool applyTransitionTimeLimit(uint32_t &transitionTime);

  // OTA WebSocket helpers
  void clearOtaSubscriptions();
  void closeOtaClients();
  int otaClientsConnected() const;

  // State/preset JSON generation (public for use in handlers)
  std::string getStateJSON();
  std::string getPresetsJSON();
  std::string getTimersJSON();

private:
  Configuration *_config;
  Scheduler *_scheduler;
  httpd_handle_t _server = nullptr;

  uint32_t _lastBroadcast = 0;
  uint32_t _lastHealthCheckMs = 0;
  uint8_t _healthProbeFailures = 0;

  // Callbacks
  void (*_powerCallback)(bool) = nullptr;
  void (*_brightnessCallback)(uint8_t) = nullptr;
  void (*_effectCallback)(uint8_t, const EffectParams &) = nullptr;
  void (*_presetCallback)(uint8_t) = nullptr;
  void (*_configCallback)() = nullptr;

  // WebSocket client state (keyed by socket fd)
  std::set<int> _otaClients;         // fds of OTA-subscribed WS clients
  std::map<int, bool> _wsHandshaked; // fd -> first-msg received?
  std::set<int> _wsBlocked;          // fds failed on send; ignore until gone
  std::map<int, uint8_t> _wsSendFailStreak; // fd -> consecutive send failures

  // Live LED broadcast tick flag (set by esp_timer callback)
  volatile bool _liveLedTick = false;

  // Latest live frame queued for async WS broadcast
  std::vector<uint8_t> _pendingLiveFrame;
  bool _liveFrameQueued = false;
  bool _liveFrameDirty = false;
  SemaphoreHandle_t _liveFrameMutex = nullptr;

  // Timer handle for live LED ticker
  esp_timer_handle_t _liveLedTimer = nullptr;

  // Internal helpers
  void setupWebSocket();
  void setupRoutes();
  void buildEffectsCache();
  bool startHttpServer();
  void recoverHttpServer();
  void runHealthCheck();
  void broadcastText(const std::string &msg, bool otaClientsOnly = false);
  void broadcastBinary(const uint8_t *data, size_t len);
  void cleanupDisconnectedClients();

  // Handlers (called from static C handlers)
  void handleSetState(httpd_req_t *req);
  void handleGetPresets(httpd_req_t *req);
  void handleSetPreset(httpd_req_t *req);
  void handleSetConfig(httpd_req_t *req);
  void handleSetTimer(httpd_req_t *req);

  // Static HTTP handler trampoline helpers
  static WebServerManager *fromReq(httpd_req_t *req) {
    return (WebServerManager *)req->user_ctx;
  }

  // Static route handlers
  static esp_err_t wsHandler(httpd_req_t *req);
  static esp_err_t hVersion(httpd_req_t *req);
  static esp_err_t hUpdateGet(httpd_req_t *req);
  static esp_err_t hUpdatePost(httpd_req_t *req);
  static esp_err_t hCommand(httpd_req_t *req);
  static esp_err_t hOtaUpload(httpd_req_t *req);
  static esp_err_t hCaptive(httpd_req_t *req);
  static esp_err_t hNoContent(httpd_req_t *req);
  static esp_err_t hIndexJs(httpd_req_t *req);
  static esp_err_t hStyleCss(httpd_req_t *req);
  static esp_err_t hRoot(httpd_req_t *req);
  static esp_err_t hWifiScan(httpd_req_t *req);
  static esp_err_t hWifiGet(httpd_req_t *req);
  static esp_err_t hWifiPost(httpd_req_t *req);
  static esp_err_t hStateGet(httpd_req_t *req);
  static esp_err_t hStatePost(httpd_req_t *req);
  static esp_err_t hEffects(httpd_req_t *req);
  static esp_err_t hPresetsGet(httpd_req_t *req);
  static esp_err_t hPresetPost(httpd_req_t *req);
  static esp_err_t hConfigGet(httpd_req_t *req);
  static esp_err_t hConfigPost(httpd_req_t *req);
  static esp_err_t hFactoryReset(httpd_req_t *req);
  static esp_err_t hTimerPost(httpd_req_t *req);
  static esp_err_t hTimezones(httpd_req_t *req);
  static esp_err_t hOptions(httpd_req_t *req);
  static esp_err_t hNotFound(httpd_req_t *req, httpd_err_code_t err);

  // Live LED timer callback
  static void liveLedTimerCb(void *arg);
  static void liveBinaryBroadcastWork(void *arg);
};

#endif // WEBSERVER_H
