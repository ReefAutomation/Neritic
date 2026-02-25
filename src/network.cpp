#include "network.h"
#include "debug.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <string>

static const char *TAG = "network";

// TEMP DEBUG: set to 0 after diagnosing STA connection/auth issues.
#define LOG_WIFI_CREDENTIALS_TEMP 1

static volatile bool  s_sta_connected       = false;
static volatile bool  s_ap_mode             = false;
static esp_netif_t   *s_sta_netif           = nullptr;
static esp_netif_t   *s_ap_netif            = nullptr;
static uint32_t       s_ap_ip               = 0;   // AP IP in network byte order
static uint32_t       s_last_disconnect_ms  = 0;  // Time of last disconnect
static uint32_t       s_failure_streak_start_ms = 0;  // When the current failure streak began
static int            s_consecutive_auth_failures = 0; // Track auth failure streaks
static bool           s_ap_fallback_active  = false;  // Whether we've fallen back to AP mode due to auth failures

#define MIN_AUTH_FAILURES_BEFORE_AP_FALLBACK 3   // Fall back to AP after 3 auth failures
#define MIN_TIME_BEFORE_AP_FALLBACK_MS 30000     // OR after 30 seconds of continuous failure

// ── Event handler ─────────────────────────────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
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
            wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)data;
            s_sta_connected = false;
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
            s_last_disconnect_ms = now;
            
            // Track when failure streak started (on first failure)
            if (s_consecutive_auth_failures == 0) {
                s_failure_streak_start_ms = now;
            }
            
            // Log disconnect reason for debugging
            const char *reason_str = "UNKNOWN";
            int reason = disconnected->reason;
            
            // Check for auth failure reason (0x200 and related codes)
            if (reason >= 2 && reason <= 8) {  // Common auth/assoc failure range
                reason_str = "AUTH_ASSOC_FAIL";
                s_consecutive_auth_failures++;
                ESP_LOGW(TAG, "Auth failure detected (count=%d)", s_consecutive_auth_failures);
                
                // Check if we should fall back to AP mode (password wrong or SSID not found)
                if (s_consecutive_auth_failures >= MIN_AUTH_FAILURES_BEFORE_AP_FALLBACK) {
                    ESP_LOGW(TAG, "Too many auth failures (%d), will fall back to AP+Captive Portal",
                             s_consecutive_auth_failures);
                    s_ap_fallback_active = true;
                }
            } else if (reason == 15) {  // WIFI_REASON_NO_AP_FOUND
                reason_str = "NO_AP_FOUND";
                s_consecutive_auth_failures++;
                ESP_LOGW(TAG, "SSID not found (count=%d)", s_consecutive_auth_failures);
                if (s_consecutive_auth_failures >= MIN_AUTH_FAILURES_BEFORE_AP_FALLBACK) {
                    ESP_LOGW(TAG, "SSID not found too many times (%d), falling back to AP+Captive Portal",
                             s_consecutive_auth_failures);
                    s_ap_fallback_active = true;
                }
            } else if (reason == 1) {  // WIFI_REASON_UNSPECIFIED
                reason_str = "UNSPECIFIED";
            } else if (reason == 201 || reason == 202) {  // Beacon timeout
                reason_str = "BEACON_TIMEOUT";
            }
            
            ESP_LOGW(TAG, "STA disconnected: reason=%d (%s) | auth_fails=%d",
                     reason, reason_str, s_consecutive_auth_failures);
            
            // Don't immediately reconnect – let networkLoop handle it with backoff
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
        } else if (id == IP_EVENT_STA_LOST_IP) {
            s_sta_connected = false;
        }
    }
}

// ── Captive-portal DNS task ────────────────────────────────────────────────────
// Responds to every DNS query with the AP IP so browsers redirect to the portal.
static void dns_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(nullptr); return; }

    struct sockaddr_in sa = {};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port        = htons(53);
    bind(sock, (struct sockaddr *)&sa, sizeof(sa));

    uint8_t buf[512];
    struct sockaddr_in cli = {};
    socklen_t cli_len = sizeof(cli);

    while (true) {
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&cli, &cli_len);
        if (len < 12) continue;

        // Build minimal DNS response that points every A query to s_ap_ip
        uint8_t resp[512];
        memcpy(resp, buf, len);
        // Set QR=1, Opcode=0, AA=1, TC=0, RD=0, RA=0, RCODE=0
        resp[2] = 0x84; resp[3] = 0x00;
        // One question, one answer
        resp[4] = 0x00; resp[5] = 0x01;
        resp[6] = 0x00; resp[7] = 0x01;
        resp[8] = 0x00; resp[9] = 0x00;
        resp[10] = 0x00; resp[11] = 0x00;

        // Append answer RR (pointer to question name + type A + IN + TTL + RDATA)
        int pos = len;
        resp[pos++] = 0xC0; resp[pos++] = 0x0C;    // name pointer to question
        resp[pos++] = 0x00; resp[pos++] = 0x01;    // type A
        resp[pos++] = 0x00; resp[pos++] = 0x01;    // class IN
        resp[pos++] = 0x00; resp[pos++] = 0x00;    // TTL
        resp[pos++] = 0x00; resp[pos++] = 0x3C;    // TTL = 60 s
        resp[pos++] = 0x00; resp[pos++] = 0x04;    // RDLENGTH = 4
        uint8_t *ip = (uint8_t *)&s_ap_ip;
        resp[pos++] = ip[0]; resp[pos++] = ip[1];
        resp[pos++] = ip[2]; resp[pos++] = ip[3];

        sendto(sock, resp, pos, 0, (struct sockaddr *)&cli, cli_len);
    }
}

// ── networkSetup ──────────────────────────────────────────────────────────────
void networkSetup(Configuration &config) {
    ESP_LOGI(TAG, "networkSetup");

    esp_netif_init();
    esp_event_loop_create_default();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));

    const std::string &ssid     = config.network.ssid;
    const std::string &password = config.network.password;
    const std::string &apPass   = config.network.apPassword;
    const std::string &hostname = config.network.hostname;

    if (!ssid.empty()) {
        ESP_LOGW(TAG, "TEMP WiFi creds debug: STA SSID='%s' PASS='%s' (len=%u)",
                 ssid.c_str(), password.c_str(), (unsigned)password.size());
    } else {
        ESP_LOGW(TAG, "TEMP WiFi creds debug: STA SSID is empty");
    }

    // Always configure AP (used as fallback / captive portal)
    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, hostname.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, apPass.c_str(), sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len       = (uint8_t)hostname.size();
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.channel        = 1;
    if (apPass.size() >= 8) {
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    if (ssid.empty()) {
        // No STA credentials – start APSTA so STA is ready once credentials are set
        ESP_LOGI(TAG, "No STA credentials, starting APSTA: %s", hostname.c_str());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_ap_mode = true;
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            s_ap_ip = ip_info.ip.addr;
        }
        xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
        return;
    }

    // STA-only mode: try to connect
    ESP_LOGI(TAG, "STA credentials found, starting STA mode: %s", ssid.c_str());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid, ssid.c_str(), sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, password.c_str(), sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_netif_set_hostname(s_sta_netif, hostname.c_str());

    // Start the failure streak timer NOW (not when first disconnect happens)
    // This ensures AP fallback happens quickly if WiFi auth fails
    s_failure_streak_start_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Short initial attempts to detect auth failures quickly (5 seconds total)
    // If auth is wrong or SSID doesn't exist, we want to fall back to AP fast
    const int SHORT_WAIT_MS = 1500;     // Very short wait for quick auth failure detection
    const int SHORT_RETRY_CYCLES = 3;   // 3 attempts × 1.5s = ~4.5 seconds
    int retry_cycle = 0;
    while (!s_sta_connected && retry_cycle < SHORT_RETRY_CYCLES && !s_ap_fallback_active) {
        int waited = 0;
        while (!s_sta_connected && waited < SHORT_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(200));
            waited += 200;
        }
        if (!s_sta_connected && !s_ap_fallback_active) {
            ESP_LOGW(TAG, "STA connect attempt %d failed, retrying...", retry_cycle + 1);
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));  // Small delay to allow disconnect event to process
            esp_wifi_connect();
            retry_cycle++;
        }
    }

    // If AP fallback was triggered during the retry loop, activate it NOW immediately
    if (s_ap_fallback_active) {
        ESP_LOGE(TAG, "Activating AP+Captive Portal immediately (auth failures detected)");
        s_ap_fallback_active = false;
        
        // Get config values
        const std::string &hostname = config.network.hostname;
        const std::string &apPass   = config.network.apPassword;
        
        // Configure AP with hostname SSID (AP netif already created above)
        wifi_config_t ap_cfg = {};
        strncpy((char *)ap_cfg.ap.ssid, hostname.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
        strncpy((char *)ap_cfg.ap.password, apPass.c_str(), sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.ssid_len       = (uint8_t)hostname.size();
        ap_cfg.ap.max_connection = 4;
        ap_cfg.ap.channel        = 1;
        if (apPass.size() >= 8) {
            ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        }
        
        // Switch to APSTA mode (uses the AP netif created above)
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        
        s_ap_mode = true;
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            s_ap_ip = ip_info.ip.addr;
        }
        
        // Start captive portal DNS task
        xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
        ESP_LOGI(TAG, "AP+Captive Portal now ACTIVE: %s", hostname.c_str());
    } else if (!s_sta_connected) {
        // If still not connected and no AP fallback, continue retrying in background
        ESP_LOGW(TAG, "STA initial connection failed, will retry in background");
    }
}

// ── networkLoop ───────────────────────────────────────────────────────────────
void networkLoop(Configuration &config) {
    static uint32_t lastReconnectAttempt = 0;
    static int      reconnectAttempts    = 0;

    if (s_ap_mode && s_sta_connected) {
        s_ap_mode = false;
        ESP_LOGI(TAG, "STA connected, disabling AP");
        esp_wifi_set_mode(WIFI_MODE_STA);
        if (s_ap_netif) {
            esp_netif_destroy(s_ap_netif);
            s_ap_netif = nullptr;
            s_ap_ip = 0;
        }
        s_ap_fallback_active = false;
        s_consecutive_auth_failures = 0;
    }

    // Handle AP fallback due to auth failures
    if (s_ap_fallback_active && !s_ap_mode) {
        ESP_LOGE(TAG, "Switching to AP+Captive Portal due to WiFi auth failures");
        s_ap_fallback_active = false;
        
        // Configure AP with hostname SSID (AP netif already created in networkSetup)
        wifi_config_t ap_cfg = {};
        const std::string &hostname = config.network.hostname;
        const std::string &apPass   = config.network.apPassword;
        strncpy((char *)ap_cfg.ap.ssid, hostname.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
        strncpy((char *)ap_cfg.ap.password, apPass.c_str(), sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.ssid_len       = (uint8_t)hostname.size();
        ap_cfg.ap.max_connection = 4;
        ap_cfg.ap.channel        = 1;
        if (apPass.size() >= 8) {
            ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        }
        
        // Switch to APSTA mode (uses the AP netif created during networkSetup)
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        
        s_ap_mode = true;
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            s_ap_ip = ip_info.ip.addr;
        }
        
        // Start captive portal DNS task
        xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
    }

    // Check if we should fall back to AP mode based on time (30+ seconds of continuous failure)
    if (!s_ap_fallback_active && !s_ap_mode && !config.network.ssid.empty() && !s_sta_connected) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (s_consecutive_auth_failures > 0) {
            uint32_t failure_duration = now - s_failure_streak_start_ms;
            if (failure_duration > MIN_TIME_BEFORE_AP_FALLBACK_MS) {
                ESP_LOGW(TAG, "WiFi failing for %lu ms, triggering AP+Captive Portal fallback",
                         failure_duration);
                s_ap_fallback_active = true;
            }
        }
    }

    if (!s_ap_mode && !config.network.ssid.empty() && !s_sta_connected && !s_ap_fallback_active) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        
        // Calculate exponential backoff based on auth failure streak
        uint32_t backoff_ms = 1000;  // 1 second default
        if (s_consecutive_auth_failures >= 6) {
            backoff_ms = 30000;  // 30 seconds after many failures
        } else if (s_consecutive_auth_failures >= 3) {
            backoff_ms = 5000;   // 5 seconds after several failures
        } else if (s_consecutive_auth_failures > 0) {
            backoff_ms = 1000;   // 1 second for first few failures
        }
        
        // Only attempt reconnect if enough time has passed since disconnect
        if ((now - s_last_disconnect_ms) > backoff_ms && 
            (now - lastReconnectAttempt) > 10000) {
            lastReconnectAttempt = now;
            reconnectAttempts++;
            ESP_LOGW(TAG, "Reconnect attempt %d (auth_fails=%d, backoff=%ldms)",
                     reconnectAttempts, s_consecutive_auth_failures, backoff_ms);
            esp_wifi_connect();
        }
        
        // Reset auth failure counter on successful connection
        if (s_sta_connected) {
            if (s_consecutive_auth_failures > 0) {
                ESP_LOGI(TAG, "Reset auth failure counter after successful connection");
                s_consecutive_auth_failures = 0;
            }
            reconnectAttempts = 0;
        }
    }
}

// ── Status helpers ────────────────────────────────────────────────────────────
bool networkIsStaConnected() { return s_sta_connected; }
bool networkIsApMode()       { return s_ap_mode; }

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
