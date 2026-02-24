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

static volatile bool s_sta_connected = false;
static volatile bool s_ap_mode       = false;
static esp_netif_t  *s_sta_netif     = nullptr;
static esp_netif_t  *s_ap_netif      = nullptr;
static uint32_t      s_ap_ip         = 0;   // AP IP in network byte order

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
        case WIFI_EVENT_STA_DISCONNECTED:
            s_sta_connected = false;
            ESP_LOGW(TAG, "STA disconnected");
            // reconnect is handled in networkLoop
            break;
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

    // Wait up to 3 cycles of 10 seconds (total 30s) for connection, retrying each cycle
    const int MAX_WAIT_MS = 10000;
    const int MAX_RETRY_CYCLES = 3;
    int retry_cycle = 0;
    while (!s_sta_connected && retry_cycle < MAX_RETRY_CYCLES) {
        int waited = 0;
        while (!s_sta_connected && waited < MAX_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(200));
            waited += 200;
        }
        if (!s_sta_connected) {
            ESP_LOGW(TAG, "STA connect attempt %d failed, retrying...", retry_cycle + 1);
            esp_wifi_disconnect();
            esp_wifi_connect();
            retry_cycle++;
        }
    }

    if (!s_sta_connected) {
        ESP_LOGW(TAG, "STA failed after retries, activating captive portal AP");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_ap_mode = true;
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            s_ap_ip = ip_info.ip.addr;
        }
        xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
    }
}

// ── networkLoop ───────────────────────────────────────────────────────────────
void networkLoop(Configuration &config) {
    static uint32_t lastCheck         = 0;
    static int      reconnectAttempts = 0;
    const  uint32_t CHECK_INTERVAL    = 10000;

    if (s_ap_mode && s_sta_connected) {
        s_ap_mode = false;
        ESP_LOGI(TAG, "STA connected, disabling AP");
        esp_wifi_set_mode(WIFI_MODE_STA);
        if (s_ap_netif) {
            esp_netif_destroy(s_ap_netif);
            s_ap_netif = nullptr;
            s_ap_ip = 0;
        }
    }

    if (!s_ap_mode && !config.network.ssid.empty()) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (!s_sta_connected && (now - lastCheck) > CHECK_INTERVAL) {
            lastCheck = now;
            reconnectAttempts++;
            ESP_LOGW(TAG, "Reconnect attempt %d", reconnectAttempts);
            esp_wifi_connect();
            if (reconnectAttempts >= 5) {
                ESP_LOGW(TAG, "Too many failures, switching to AP mode");
                s_ap_mode = true;
                reconnectAttempts = 0;
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
                    s_ap_ip = ip_info.ip.addr;
                }
                xTaskCreate(dns_task, "dns_task", 4096, nullptr, 5, nullptr);
            }
        } else if (s_sta_connected) {
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
