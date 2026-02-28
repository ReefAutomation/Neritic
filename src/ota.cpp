#define DEEPGLOW_REPO_URL "https://github.com/kabroxiko/DeepGlow"

#include "ota.h"
#include "debug.h"
#include "webserver.h"
#include "config.h"
#include "scheduler.h"
#include "transition.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <string>
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#if defined(ESP_IDF_VERSION_MAJOR)
#include <ArduinoJson.h>
#endif
#include "uzlib.h"

static const char *TAG = "ota";

extern WebServerManager *webServerPtr;

volatile bool otaInProgress = false;
volatile bool otaRequested  = false;
volatile bool otaAckReceived = false;

// ── OTA status broadcast ───────────────────────────────────────────────────────
static void broadcastOtaStatus(const std::string &status,
                                const std::string &msg, int progress) {
    if (progress >= 0)
        ESP_LOGI(TAG, "OTA status=%s msg=%s progress=%d",
                 status.c_str(), msg.c_str(), progress);
    else
        ESP_LOGI(TAG, "OTA status=%s msg=%s", status.c_str(), msg.c_str());

    if (webServerPtr) {
        if (progress >= 0)
            webServerPtr->broadcastOtaStatus(status, msg, progress);
        else
            webServerPtr->broadcastOtaStatus(status, msg);
    }
}

// ── No-op stubs for ArduinoOTA compatibility ───────────────────────────────────
void setupArduinoOTA(const char * /* hostname */) {}
void handleArduinoOTA() {}

// ── HTTPS helper: fetch URL into a std::string ─────────────────────────────────
// Uses esp_http_client_perform so that:
//  • HTTP redirects (302) are followed automatically via max_redirection_count
//  • Chunked transfer-encoded responses (Content-Length == -1) are handled
static esp_err_t _httpsGetEventHandler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        auto *out = static_cast<std::string *>(evt->user_data);
        out->append(static_cast<char *>(evt->data), (size_t)evt->data_len);
    }
    return ESP_OK;
}

static std::string httpsGet(const char *url) {
    std::string result;
    esp_http_client_config_t cfg = {};
    cfg.url                   = url;
    cfg.crt_bundle_attach     = esp_crt_bundle_attach;
    cfg.method                = HTTP_METHOD_GET;
    cfg.max_redirection_count = 10;
    cfg.buffer_size           = 4096; // GitHub CDN redirect Location header can exceed 512-byte default
    cfg.buffer_size_tx        = 1024;
    cfg.event_handler         = _httpsGetEventHandler;
    cfg.user_data             = &result;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return result;

    esp_err_t err = esp_http_client_perform(client);
    int code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || code != 200) {
        ESP_LOGW(TAG, "httpsGet %s → err=%d code=%d", url, err, code);
        result.clear();
    }
    return result;
}

// ── Manifest helpers ───────────────────────────────────────────────────────────
std::string fetchRemoteManifestJson() {
    const char *url = DEEPGLOW_REPO_URL "/releases/latest/download/manifest.json";
    return httpsGet(url);
}

std::string getLatestFirmwareUrl(std::string &latestVersion) {
    std::string payload = fetchRemoteManifestJson();
    if (payload.empty()) { latestVersion = ""; return ""; }

    #if defined(ESP_IDF_VERSION_MAJOR)
        DynamicJsonDocument doc(2048);
        if (deserializeJson(doc, payload)) { latestVersion = ""; return ""; }

        const char *targetEnv = OTA_ENV;
        for (JsonVariant entry : doc.as<JsonArray>()) {
            if (strcmp(entry["env"] | "", targetEnv) == 0) {
                latestVersion = entry["version"] | "";
                std::string url  = entry["url"] | "";
                return url;
            }
        }
        latestVersion = "";
        return "";
    #else
        // Arduino: manual JSON parsing (assumes manifest.json is a flat array of objects)
        latestVersion = "";
        std::string url = "";
        size_t pos = 0;
        const std::string envKey = "\"env\":\"";
        const std::string versionKey = "\"version\":\"";
        const std::string urlKey = "\"url\":\"";
        while ((pos = payload.find(envKey, pos)) != std::string::npos) {
            size_t envStart = pos + envKey.length();
            size_t envEnd = payload.find("\"", envStart);
            std::string envVal = payload.substr(envStart, envEnd - envStart);
            if (envVal == OTA_ENV) {
                // Find version
                size_t versionPos = payload.find(versionKey, envEnd);
                if (versionPos != std::string::npos) {
                    size_t versionStart = versionPos + versionKey.length();
                    size_t versionEnd = payload.find("\"", versionStart);
                    latestVersion = payload.substr(versionStart, versionEnd - versionStart);
                }
                // Find url
                size_t urlPos = payload.find(urlKey, envEnd);
                if (urlPos != std::string::npos) {
                    size_t urlStart = urlPos + urlKey.length();
                    size_t urlEnd = payload.find("\"", urlStart);
                    url = payload.substr(urlStart, urlEnd - urlStart);
                }
                return url;
            }
            pos = envEnd;
        }
        return "";
    #endif
}

// ── uzlib source callbacks ────────────────────────────────────────────────────
// Use uzlib stream callback API from PlatformIO libdeps.

// Remote HTTP source
static esp_http_client_handle_t s_stream_client = nullptr;
static unsigned char s_http_buf[4096];
static int           s_http_pos = 0, s_http_len = 0;

static int httpReadSourceByte(TINF_DATA *d) {
    (void)d;
    if (s_http_pos >= s_http_len) {
        int rd = esp_http_client_read(s_stream_client,
                                       (char *)s_http_buf, sizeof(s_http_buf));
        if (rd <= 0) return -1;
        s_http_len = rd; s_http_pos = 0;
    }
    return s_http_buf[s_http_pos++];
}

// Local file source
static FILE         *s_gz_file        = nullptr;
static unsigned char s_file_buf[4096];
static int           s_file_pos        = 0, s_file_len = 0;
static int           s_file_read_total = 0;   // tracks input bytes consumed (for progress)

static int fileReadSourceByte(TINF_DATA *d) {
    (void)d;
    if (s_file_pos >= s_file_len) {
        s_file_len = (int)fread(s_file_buf, 1, sizeof(s_file_buf), s_gz_file);
        s_file_pos = 0;
        if (s_file_len <= 0) return -1;
    }
    int value = s_file_buf[s_file_pos++];
    s_file_read_total++;
    return value;
}

// ── gz write callback – uses esp_ota_ops ──────────────────────────────────────
static esp_ota_handle_t     s_ota_handle    = 0;
static const esp_partition_t *s_ota_part    = nullptr;
static bool                 s_ota_started   = false;
static size_t               s_ota_written   = 0;
static int                  s_ota_total_est = 0;

static bool gzWriteCallback(unsigned char *buff, size_t buffsize) {
    if (!s_ota_started) {
        s_ota_part = esp_ota_get_next_update_partition(NULL);
        if (!s_ota_part) {
            ESP_LOGE(TAG, "No OTA partition");
            return false;
        }
        if (esp_ota_begin(s_ota_part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed");
            return false;
        }
        s_ota_started = true;
        s_ota_written = 0;
    }
    if (esp_ota_write(s_ota_handle, buff, buffsize) != ESP_OK) return false;
    s_ota_written += buffsize;
    if (s_ota_total_est > 0) {
        int pct = (int)(s_ota_written * 100 / (size_t)s_ota_total_est);
        if (pct > 100) pct = 100;
        static int lastPct = -1;
        if (pct != lastPct) { broadcastOtaStatus("progress", "", pct); lastPct = pct; }
    }
    return true;
}

// ── Redirect resolver: follow 301/302/307/308 and return final URL ────────────
// esp_http_client_open() does NOT follow redirects automatically.
struct _ResolveCtx { std::string location; };
static esp_err_t _resolveEventHandler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        strcasecmp(evt->header_key, "Location") == 0)
        static_cast<_ResolveCtx *>(evt->user_data)->location = evt->header_value;
    return ESP_OK;
}
static std::string resolveFinalUrl(const char *url) {
    std::string current = url;
    for (int hop = 0; hop < 10; hop++) {
        _ResolveCtx ctx;
        esp_http_client_config_t cfg = {};
        cfg.url               = current.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.method            = HTTP_METHOD_GET;
        cfg.buffer_size       = 4096;
        cfg.buffer_size_tx    = 1024;
        cfg.timeout_ms        = 10000;
        cfg.event_handler     = _resolveEventHandler;
        cfg.user_data         = &ctx;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) break;
        bool opened = (esp_http_client_open(client, 0) == ESP_OK);
        int code = 0;
        if (opened) { esp_http_client_fetch_headers(client); code = esp_http_client_get_status_code(client); esp_http_client_close(client); }
        esp_http_client_cleanup(client);
        if (!opened) break;
        if (code == 200) return current;
        if ((code==301||code==302||code==303||code==307||code==308) && !ctx.location.empty()) {
            ESP_LOGI(TAG, "Redirect %d → %s", code, ctx.location.c_str());
            current = ctx.location;
        } else break;
    }
    return current;
}

// ── Remote gz OTA: streaming HTTP → uzlib → flash ─────────────────────────────
bool performGzOtaUpdate(std::string &errorOut) {
    otaInProgress = true;
    s_ota_started = false;
    s_ota_written = 0;

    broadcastOtaStatus("start", "OTA update started", -1);

    std::string latestVersion;
    std::string firmwareUrl = getLatestFirmwareUrl(latestVersion);
    if (firmwareUrl.empty()) {
        errorOut = "Could not get firmware URL";
        otaInProgress = false;
        broadcastOtaStatus("error", errorOut, -1);
        return false;
    }
    ESP_LOGI(TAG, "Firmware URL: %s", firmwareUrl.c_str());

    // Follow GitHub → CDN redirect chain; open() doesn't do this automatically.
    std::string resolvedUrl = resolveFinalUrl(firmwareUrl.c_str());
    ESP_LOGI(TAG, "Resolved URL:  %s", resolvedUrl.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url               = resolvedUrl.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.method            = HTTP_METHOD_GET;
    cfg.timeout_ms        = 30000;
    cfg.buffer_size       = 8192;
    cfg.buffer_size_tx    = 4096;  // CDN redirect URLs can exceed 1500 chars

    s_stream_client = esp_http_client_init(&cfg);
    if (!s_stream_client) {
        otaInProgress = false; errorOut = "http init failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    if (esp_http_client_open(s_stream_client, 0) != ESP_OK) {
        esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "http open failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    int64_t clen = esp_http_client_fetch_headers(s_stream_client);
    int code     = esp_http_client_get_status_code(s_stream_client);
    if (code != 200) {  // clen may be -1 for chunked; that is acceptable
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "HTTP error " + std::to_string(code);
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    // clen > 0: use as decompressed-size estimate; -1 (chunked): disable % display
    s_ota_total_est = (clen > 0) ? (int)((int64_t)clen * 3 / 2) : 0;

    // ── Set up uzlib with readSourceByte callback (source=NULL required) ────────
    s_http_pos = 0; s_http_len = 0;
    unsigned int dictSize = 32768;
    unsigned char *dict = (unsigned char *)malloc(dictSize);
    if (!dict) {
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "malloc failed for dict";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }

    TINF_DATA d = {};
    uzlib_init();
    d.source         = nullptr;
    d.source_limit   = nullptr;
    d.source_read_cb = httpReadSourceByte;

    if (uzlib_gzip_parse_header(&d) != TINF_OK) {
        free(dict);
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "gzip header parse failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    uzlib_uncompress_init(&d, dict, dictSize);

    // ── Decompress in 4 KB chunks and write to OTA flash ─────────────────────
    const size_t OUT_CHUNK = 4096;
    uint8_t *outbuf = (uint8_t *)malloc(OUT_CHUNK);
    if (!outbuf) {
        free(dict);
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "malloc outbuf failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }

    bool ok = true;
    int ret = TINF_OK;
    while (ret == TINF_OK) {
        d.dest          = outbuf;
        d.destStart     = outbuf;
        d.dest_limit    = outbuf + OUT_CHUNK;
        ret = uzlib_uncompress(&d);
        size_t produced = (size_t)(d.dest - outbuf);
        if (produced > 0) {
            if (!gzWriteCallback(outbuf, produced)) {
                ok = false;
                if (errorOut.empty()) errorOut = "OTA write failed";
                break;
            }
        }
        if (ret == TINF_DONE) break;
        if (ret < 0) { ok = false; errorOut = "gzip decompress error " + std::to_string(ret); break; }
    }
    free(outbuf); free(dict);
    esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;

    if (!ok || !s_ota_started) {
        if (s_ota_started) esp_ota_abort(s_ota_handle);
        otaInProgress = false;
        if (errorOut.empty()) errorOut = "gz decompression/flash failed";
        broadcastOtaStatus("error", errorOut, -1);
        return false;
    }

    if (esp_ota_end(s_ota_handle) != ESP_OK) {
        otaInProgress = false; errorOut = "esp_ota_end failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    if (esp_ota_set_boot_partition(s_ota_part) != ESP_OK) {
        otaInProgress = false; errorOut = "set_boot_partition failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }

    otaInProgress = false;
    broadcastOtaStatus("progress", "", 100);
    broadcastOtaStatus("success", "OTA update successful", -1);
    return true;
}

// ── Local firmware upload handler: auto-detects .bin or .bin.gz ─────────────
// .bin.gz: saves to LittleFS first, then decompresses (same strategy as original)
// .bin:    flashes directly while receiving
esp_err_t handleOtaUpload(httpd_req_t *req) {
    otaInProgress = true;
    broadcastOtaStatus("start", "Local OTA upload started", -1);

    // Read first chunk to detect file type
    char buf[4096];
    int firstRecv = httpd_req_recv(req, buf, sizeof(buf));
    if (firstRecv < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        otaInProgress = false; return ESP_FAIL;
    }

    const bool isGzip = (firstRecv >= 2 &&
                         (uint8_t)buf[0] == 0x1F && (uint8_t)buf[1] == 0x8B);
    int total = req->content_len;   // may be -1 if chunked

    if (isGzip) {
        // ── Step 1: receive whole file to LittleFS ────────────────────────────
        const char *tmpPath = "/data/ota_upload.bin.gz";
        FILE *fout = fopen(tmpPath, "wb");
        if (!fout) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tmpfile open failed");
            otaInProgress = false; return ESP_FAIL;
        }
        if (firstRecv > 0) fwrite(buf, 1, firstRecv, fout);
        int received = firstRecv;
        int uploadLastPct = -1;
        for (;;) {
            int rd = httpd_req_recv(req, buf, sizeof(buf));
            if (rd == 0) break;
            if (rd < 0) {
                if (rd == HTTPD_SOCK_ERR_TIMEOUT) continue;
                fclose(fout); remove(tmpPath);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
                otaInProgress = false; return ESP_FAIL;
            }
            fwrite(buf, 1, rd, fout);
            received += rd;
            // Phase 1: upload progress 0-49% based on bytes received
            if (total > 0) {
                int pct = received * 49 / total;
                if (pct > 49) pct = 49;
                if (pct != uploadLastPct) { broadcastOtaStatus("progress", "", pct); uploadLastPct = pct; }
            }
        }
        fclose(fout);
        ESP_LOGI(TAG, "gz upload saved: %d bytes → decompressing", received);

        // ── Step 2: decompress from file → OTA flash ─────────────────────────
        const esp_partition_t *ota_part = esp_ota_get_next_update_partition(NULL);
        esp_ota_handle_t ota_handle = 0;
        if (!ota_part || esp_ota_begin(ota_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle) != ESP_OK) {
            remove(tmpPath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
            otaInProgress = false; return ESP_FAIL;
        }

        s_gz_file = fopen(tmpPath, "rb");
        if (!s_gz_file) {
            remove(tmpPath); esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tmpfile reopen failed");
            otaInProgress = false; return ESP_FAIL;
        }

        unsigned char *dict   = (unsigned char *)malloc(32768);
        uint8_t       *outbuf = (uint8_t *)malloc(4096);
        TINF_DATA     *dp     = (TINF_DATA *)calloc(1, sizeof(TINF_DATA));
        if (!dict || !outbuf || !dp) {
            free(dict); free(outbuf); free(dp);
            fclose(s_gz_file); s_gz_file = nullptr;
            remove(tmpPath); esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "malloc failed");
            otaInProgress = false; return ESP_FAIL;
        }

        s_file_pos = 0; s_file_len = 0; s_file_read_total = 0;
        uzlib_init();
        dp->source         = nullptr;
        dp->source_limit   = nullptr;
        dp->source_read_cb = fileReadSourceByte;

        if (uzlib_gzip_parse_header(dp) != TINF_OK) {
            free(dict); free(outbuf); free(dp);
            fclose(s_gz_file); s_gz_file = nullptr;
            remove(tmpPath); esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "gzip header error");
            otaInProgress = false; return ESP_FAIL;
        }
        uzlib_uncompress_init(dp, dict, 32768);

        // Phase 2: decompress progress 50-99% (if upload phase known) or 0-99%
        // Progress is based on input bytes read from gz file (like original setGzProgressCallback)
        const int progressBase = (total > 0) ? 50 : 0;
        const int progressRange = 99 - progressBase;  // 49 or 99
        int written_total = 0, lastPct = progressBase - 1, ret = TINF_OK;
        bool ok = true;
        broadcastOtaStatus("progress", "", progressBase); lastPct = progressBase;
        while (ret == TINF_OK) {
            dp->dest = outbuf; dp->destStart = outbuf;
            dp->dest_limit = outbuf + 4096;
            ret = uzlib_uncompress(dp);
            size_t produced = (size_t)(dp->dest - outbuf);
            if (produced > 0) {
                if (esp_ota_write(ota_handle, outbuf, produced) != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed at %d bytes", written_total);
                    ok = false; break;
                }
                written_total += (int)produced;
            }
            // Track progress by input consumed (exact, like original's setGzProgressCallback)
            if (received > 0) {
                int pct = progressBase + s_file_read_total * progressRange / received;
                if (pct > 99) pct = 99;
                if (pct != lastPct) { broadcastOtaStatus("progress", "", pct); lastPct = pct; }
            }
            if (ret == TINF_DONE) break;
            if (ret < 0) { ESP_LOGE(TAG, "uzlib error %d", ret); ok = false; break; }
        }
        free(dp); free(dict); free(outbuf);
        fclose(s_gz_file); s_gz_file = nullptr;
        remove(tmpPath);

        if (!ok) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "gz decompress/flash failed");
            otaInProgress = false; return ESP_FAIL;
        }
        if (esp_ota_end(ota_handle) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end failed");
            otaInProgress = false; return ESP_FAIL;
        }
        if (esp_ota_set_boot_partition(ota_part) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot failed");
            otaInProgress = false; return ESP_FAIL;
        }
    } else {
        // ── .bin: flash while receiving ───────────────────────────────────────
        const esp_partition_t *ota_part = esp_ota_get_next_update_partition(NULL);
        esp_ota_handle_t ota_handle = 0;
        if (!ota_part || esp_ota_begin(ota_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
            otaInProgress = false; return ESP_FAIL;
        }
        int written_total = 0, lastPct = -1;
        if (firstRecv > 0) {
            if (esp_ota_write(ota_handle, (const void *)buf, firstRecv) != ESP_OK) {
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write error");
                otaInProgress = false; return ESP_FAIL;
            }
            written_total = firstRecv;
        }
        for (;;) {
            int recv = httpd_req_recv(req, buf, sizeof(buf));
            if (recv == 0) break;
            if (recv < 0) {
                if (recv == HTTPD_SOCK_ERR_TIMEOUT) continue;
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
                otaInProgress = false; return ESP_FAIL;
            }
            if (esp_ota_write(ota_handle, (const void *)buf, recv) != ESP_OK) {
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write error");
                otaInProgress = false; return ESP_FAIL;
            }
            written_total += recv;
            if (total > 0) {
                int pct = written_total * 100 / total;
                if (pct != lastPct) { broadcastOtaStatus("progress", "", pct); lastPct = pct; }
            }
        }
        if (esp_ota_end(ota_handle) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end failed");
            otaInProgress = false; return ESP_FAIL;
        }
        if (esp_ota_set_boot_partition(ota_part) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot failed");
            otaInProgress = false; return ESP_FAIL;
        }
    }

    otaInProgress = false;
    broadcastOtaStatus("progress", "", 100);
    broadcastOtaStatus("success", "OTA successful – rebooting", -1);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Rebooting\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ── otaTask: spawned by webserver POST /api/update ─────────────────────────────────
extern "C" void otaTask(void *parameter) {
    // Do NOT subscribe to the task WDT: TLS handshakes legitimately take
    // several seconds; the HTTP 30 s timeout guards against true hangs.
    (void)parameter;
    std::string error;
    bool ok = performGzOtaUpdate(error);
    if (ok) {
        otaAckReceived = false;
        uint64_t start = esp_timer_get_time() / 1000ULL;
        while ((esp_timer_get_time() / 1000ULL - start) < 3000) {
            if (otaAckReceived) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (webServerPtr) webServerPtr->closeOtaClients();
        start = esp_timer_get_time() / 1000ULL;
        while ((esp_timer_get_time() / 1000ULL - start) < 2000) {
            if (webServerPtr && webServerPtr->otaClientsConnected() == 0) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", error.c_str());
        broadcastOtaStatus("error", error.empty() ? "OTA failed" : error, -1);
    }
    vTaskDelete(NULL);
}
