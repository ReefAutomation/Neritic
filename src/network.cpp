#include "network.h"
#include "onboard_led.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <string.h>
#include <string>

static const char *TAG = "network";

// TEMP DEBUG: set to 0 after diagnosing STA connection/auth issues.
#define LOG_WIFI_CREDENTIALS_TEMP 1

static volatile bool s_sta_connected = false;
static volatile bool s_ap_mode = false;
static esp_netif_t *s_sta_netif = nullptr;
static esp_netif_t *s_ap_netif = nullptr;
static uint32_t s_ap_ip = 0;              // AP IP in network byte order
static uint32_t s_last_disconnect_ms = 0; // Time of last disconnect
static uint32_t s_failure_streak_start_ms =
    0; // When the current STA failure streak began
static bool s_dns_task_started = false;

#define STA_RECONNECT_INTERVAL_MS 5000
#define STA_FAST_RETRY_INTERVAL_MS 1000
#define STA_FAST_RETRY_WINDOW_MS 15000
#define AP_FALLBACK_DELAY_MS 120000

static void dns_task(void *arg);

static void disable_wifi_power_save() {
  esp_err_t rc = esp_wifi_set_ps(WIFI_PS_NONE);
  if (rc == ESP_OK) {
    ESP_LOGI(TAG, "WiFi power save disabled (WIFI_PS_NONE)");
  } else {
    ESP_LOGW(TAG, "Failed to disable WiFi power save: rc=%d", rc);
  }
}

static void configure_ap(const Configuration &config) {
  wifi_config_t ap_cfg = {};
  const std::string &hostname = config.network.hostname;
  const std::string &apPass = config.network.apPassword;

  strncpy((char *)ap_cfg.ap.ssid, hostname.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
  strncpy((char *)ap_cfg.ap.password, apPass.c_str(),
          sizeof(ap_cfg.ap.password) - 1);
  ap_cfg.ap.ssid_len = (uint8_t)hostname.size();
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.channel = 1;
  ap_cfg.ap.authmode =
      (apPass.size() >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
}

static void ensure_ap_enabled(const Configuration &config) {
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  configure_ap(config);
  s_ap_mode = true;

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
    s_ap_ip = ip_info.ip.addr;
  }

  if (!s_dns_task_started) {
    xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
    s_dns_task_started = true;
  }
}

static void ensure_ap_disabled() {
  if (!s_ap_mode)
    return;
  ESP_LOGI(TAG, "STA connected, disabling AP");
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  s_ap_mode = false;
  s_ap_ip = 0;
}

// ── Event handler
// ─────────────────────────────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
  if (base == WIFI_EVENT) {
    switch (id) {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      ESP_LOGI(TAG, "STA start – connecting");
      break;
    case WIFI_EVENT_STA_CONNECTED:
      ESP_LOGI(TAG, "STA connected");
      break;
    case WIFI_EVENT_STA_DISCONNECTED: {
      wifi_event_sta_disconnected_t *disconnected =
          (wifi_event_sta_disconnected_t *)data;
      s_sta_connected = false;
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
      s_last_disconnect_ms = now;

      if (s_failure_streak_start_ms == 0) {
        s_failure_streak_start_ms = now;
      }

      const char *reason_str = "UNKNOWN";
      int reason = disconnected->reason;

      if (reason >= 2 && reason <= 8) {
        reason_str = "AUTH_ASSOC_FAIL";
      } else if (reason == 15) {
        reason_str = "NO_AP_FOUND";
      } else if (reason == 1) {
        reason_str = "UNSPECIFIED";
      } else if (reason == 201 || reason == 202) {
        reason_str = "BEACON_TIMEOUT";
      }

      ESP_LOGW(TAG, "STA disconnected: reason=%d (%s)", reason, reason_str);
      break;
    }
    case WIFI_EVENT_AP_START:
      ESP_LOGI(TAG, "AP started");
      s_ap_mode = true;
      break;
    case WIFI_EVENT_AP_STOP:
      s_ap_mode = false;
      break;
    default:
      break;
    }
  } else if (base == IP_EVENT) {
    if (id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
      ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
      s_sta_connected = true;
      s_failure_streak_start_ms = 0;
    } else if (id == IP_EVENT_STA_LOST_IP) {
      s_sta_connected = false;
      #ifdef ONBOARD_RGB_LED
      setOnboardRgbLedRed(); // RED when lost IP (even if still connected at WiFi level)
      #endif
      if (s_failure_streak_start_ms == 0) {
        s_failure_streak_start_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
      }
    }
  }
}

// ── Captive-portal DNS task
// ──────────────────────────────────────────────────── Responds to every DNS
// query with the AP IP so browsers redirect to the portal.
static void dns_task(void *arg) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    vTaskDelete(nullptr);
    return;
  }

  struct sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons(53);
  bind(sock, (struct sockaddr *)&sa, sizeof(sa));

  uint8_t buf[512];
  struct sockaddr_in cli = {};
  socklen_t cli_len = sizeof(cli);

  while (true) {
    int len =
        recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cli_len);
    if (len < 12)
      continue;

    // Build minimal DNS response that points every A query to s_ap_ip
    uint8_t resp[512];
    memcpy(resp, buf, len);
    // Set QR=1, Opcode=0, AA=1, TC=0, RD=0, RA=0, RCODE=0
    resp[2] = 0x84;
    resp[3] = 0x00;
    // One question, one answer
    resp[4] = 0x00;
    resp[5] = 0x01;
    resp[6] = 0x00;
    resp[7] = 0x01;
    resp[8] = 0x00;
    resp[9] = 0x00;
    resp[10] = 0x00;
    resp[11] = 0x00;

    // Append answer RR (pointer to question name + type A + IN + TTL + RDATA)
    int pos = len;
    resp[pos++] = 0xC0;
    resp[pos++] = 0x0C; // name pointer to question
    resp[pos++] = 0x00;
    resp[pos++] = 0x01; // type A
    resp[pos++] = 0x00;
    resp[pos++] = 0x01; // class IN
    resp[pos++] = 0x00;
    resp[pos++] = 0x00; // TTL
    resp[pos++] = 0x00;
    resp[pos++] = 0x3C; // TTL = 60 s
    resp[pos++] = 0x00;
    resp[pos++] = 0x04; // RDLENGTH = 4
    uint8_t *ip = (uint8_t *)&s_ap_ip;
    resp[pos++] = ip[0];
    resp[pos++] = ip[1];
    resp[pos++] = ip[2];
    resp[pos++] = ip[3];

    sendto(sock, resp, pos, 0, (struct sockaddr *)&cli, cli_len);
  }
}

// ── networkSetup
// ──────────────────────────────────────────────────────────────
void networkSetup(Configuration &config) {
  ESP_LOGI(TAG, "networkSetup");

  esp_netif_init();
  esp_event_loop_create_default();
  s_sta_netif = esp_netif_create_default_wifi_sta();
  s_ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));

  const std::string &ssid = config.network.ssid;
  const std::string &password = config.network.password;
  const std::string &hostname = config.network.hostname;

  if (!ssid.empty()) {
    ESP_LOGW(TAG, "TEMP WiFi creds debug: STA SSID='%s' PASS='%s' (len=%u)",
             ssid.c_str(), password.c_str(), (unsigned)password.size());
  } else {
    ESP_LOGW(TAG, "TEMP WiFi creds debug: STA SSID is empty");
  }

  if (ssid.empty()) {
    // No STA credentials – keep AP enabled.
    ESP_LOGI(TAG, "No STA credentials, starting APSTA: %s", hostname.c_str());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    configure_ap(config);
    ESP_ERROR_CHECK(esp_wifi_start());
    disable_wifi_power_save();
    s_ap_mode = true;
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
      s_ap_ip = ip_info.ip.addr;
    }
    if (!s_dns_task_started) {
      xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
      s_dns_task_started = true;
    }
    return;
  }

  // STA mode with fallback AP handled in networkLoop.
  ESP_LOGI(TAG, "STA credentials found, starting STA mode: %s", ssid.c_str());
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  wifi_config_t sta_cfg = {};
  strncpy((char *)sta_cfg.sta.ssid, ssid.c_str(), sizeof(sta_cfg.sta.ssid) - 1);
  strncpy((char *)sta_cfg.sta.password, password.c_str(),
          sizeof(sta_cfg.sta.password) - 1);
  sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  disable_wifi_power_save();
  esp_netif_set_hostname(s_sta_netif, hostname.c_str());
  s_last_disconnect_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  s_failure_streak_start_ms = s_last_disconnect_ms;
}

// ── networkLoop
// ───────────────────────────────────────────────────────────────
void networkLoop(Configuration &config) {
  static uint32_t last_reconnect_attempt_ms = 0;

  const bool has_ssid = !config.network.ssid.empty();
  const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

  if (!has_ssid) {
    if (!s_ap_mode) {
      ensure_ap_enabled(config);
    }
    return;
  }

  if (s_sta_connected) {
    s_failure_streak_start_ms = 0;
    ensure_ap_disabled();
    return;
  }

  if (s_failure_streak_start_ms == 0) {
    s_failure_streak_start_ms = now;
  }

  if (!s_ap_mode && (now - s_failure_streak_start_ms) >= AP_FALLBACK_DELAY_MS) {
    ESP_LOGW(
        TAG,
        "STA not connected for %lu ms, enabling AP fallback while retrying STA",
        (unsigned long)(now - s_failure_streak_start_ms));
    ensure_ap_enabled(config);
  }

  uint32_t reconnect_interval_ms = STA_RECONNECT_INTERVAL_MS;
  if ((now - s_last_disconnect_ms) < STA_FAST_RETRY_WINDOW_MS) {
    reconnect_interval_ms = STA_FAST_RETRY_INTERVAL_MS;
  }

  uint32_t last_action_ms = last_reconnect_attempt_ms;
  if (last_action_ms < s_last_disconnect_ms) {
    last_action_ms = s_last_disconnect_ms;
  }

  if ((now - last_action_ms) >= reconnect_interval_ms) {
    last_reconnect_attempt_ms = now;
    ESP_LOGW(TAG, "Retrying STA connection%s (interval=%lums)",
             s_ap_mode ? " (AP fallback active)" : "",
             (unsigned long)reconnect_interval_ms);
    esp_wifi_connect();
  }
}

// ── Status helpers
// ────────────────────────────────────────────────────────────
bool networkIsStaConnected() { return s_sta_connected; }
bool networkIsApMode() { return s_ap_mode; }

std::string getCurrentIpString(const Configuration &config) {
  if (!s_sta_connected) {
    if (s_ap_mode && s_ap_netif) {
      esp_netif_ip_info_t info;
      if (esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
        char buf[16];
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
        return std::string(buf);
      }
    }
    return "0.0.0.0";
  }
  if (s_sta_netif) {
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK) {
      char buf[16];
      snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
      return std::string(buf);
    }
  }
  return "0.0.0.0";
}
