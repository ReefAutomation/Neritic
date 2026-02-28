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
#include "esp_heap_caps.h"
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
#include <zlib.h>

static const char *TAG = "ota";

#define OTA_STR_HELPER(x) #x
#define OTA_STR(x) OTA_STR_HELPER(x)

static inline bool isNonRetriableHttpStatus(int code) {
    return code == 404;
}

extern WebServerManager *webServerPtr;

volatile bool otaInProgress = false;
volatile bool otaRequested  = false;
volatile bool otaAckReceived = false;

static int      s_last_progress_emit = -1;
static uint64_t s_last_progress_emit_ms = 0;

// ── OTA status broadcast ───────────────────────────────────────────────────────
static void broadcastOtaStatus(const std::string &status,
                                const std::string &msg, int progress) {
    if (status == "start") {
        s_last_progress_emit = -1;
        s_last_progress_emit_ms = 0;
    }

    bool shouldBroadcast = true;
    if (status == "progress" && progress >= 0) {
        uint64_t nowMs = esp_timer_get_time() / 1000ULL;
        bool forceEmit = (progress <= 1 || progress >= 99);
        bool enoughDelta = (s_last_progress_emit < 0) || ((progress - s_last_progress_emit) >= 4);
        bool enoughTime = (s_last_progress_emit_ms == 0) || ((nowMs - s_last_progress_emit_ms) >= 900);
        shouldBroadcast = forceEmit || enoughDelta || enoughTime;
        if (shouldBroadcast) {
            s_last_progress_emit = progress;
            s_last_progress_emit_ms = nowMs;
        }
    }

    if (!shouldBroadcast) return;

    if (progress >= 0) {
        ESP_LOGI(TAG, "OTA status=%s msg=%s progress=%d",
                 status.c_str(), msg.c_str(), progress);
    } else {
        ESP_LOGI(TAG, "OTA status=%s msg=%s", status.c_str(), msg.c_str());
    }

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

static std::string httpsGet(const char *url, int *httpStatusOut = nullptr) {
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
    if (!client) {
        if (httpStatusOut) *httpStatusOut = -1;
        return result;
    }

    esp_err_t err = esp_http_client_perform(client);
    int code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (httpStatusOut) *httpStatusOut = code;

    if (err != ESP_OK || code != 200) {
        ESP_LOGW(TAG, "httpsGet %s → err=%d code=%d", url, err, code);
        result.clear();
    }
    return result;
}

// ── Manifest helpers ───────────────────────────────────────────────────────────
std::string fetchRemoteManifestJson() {
    const char *url = DEEPGLOW_REPO_URL "/releases/latest/download/manifest.json";
    std::string manifest;
    const int maxAttempts = 4;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        int httpCode = -1;
        manifest = httpsGet(url, &httpCode);
        if (!manifest.empty()) return manifest;

        if (isNonRetriableHttpStatus(httpCode)) {
            ESP_LOGW(TAG, "Manifest not found (HTTP 404), skipping retries");
            break;
        }

        if (attempt + 1 < maxAttempts) {
            TickType_t waitTicks = pdMS_TO_TICKS(150 * (attempt + 1));
            ESP_LOGW(TAG, "Manifest fetch retry %d/%d after %lu ms", attempt + 1,
                     maxAttempts - 1, (unsigned long)(waitTicks * portTICK_PERIOD_MS));
            vTaskDelay(waitTicks);
        }
    }
    return "";
}

std::string getLatestFirmwareUrl(std::string &latestVersion) {
    std::string payload = fetchRemoteManifestJson();
    if (payload.empty()) { latestVersion = ""; return ""; }

    #if defined(ESP_IDF_VERSION_MAJOR)
        DynamicJsonDocument doc(2048);
        if (deserializeJson(doc, payload)) { latestVersion = ""; return ""; }

        const char *targetEnv = OTA_STR(OTA_ENV);
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
            if (envVal == OTA_STR(OTA_ENV)) {
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

// ── zlib streaming helpers ────────────────────────────────────────────────────

static esp_http_client_handle_t s_stream_client = nullptr;

struct HttpReadContext {
    esp_http_client_handle_t client;
    const uint8_t *prefetch;
    int prefetch_len;
    int prefetch_pos;
};

struct FileReadContext {
    FILE *file;
};

static bool gzWriteCallback(unsigned char *buff, size_t buffsize);

static int readHttpChunk(uint8_t *out, size_t outLen, void *ctxPtr) {
    auto *ctx = static_cast<HttpReadContext *>(ctxPtr);
    if (ctx->prefetch && ctx->prefetch_pos < ctx->prefetch_len) {
        int avail = ctx->prefetch_len - ctx->prefetch_pos;
        int take = (int)MIN((int)outLen, avail);
        memcpy(out, ctx->prefetch + ctx->prefetch_pos, (size_t)take);
        ctx->prefetch_pos += take;
        return take;
    }

    int rd = -1;
    for (int attempt = 0; attempt < 8; ++attempt) {
        rd = esp_http_client_read(ctx->client, (char *)out, outLen);
        if (rd > 0 || rd == 0) break;
        if (attempt < 7) vTaskDelay(pdMS_TO_TICKS(50));
    }
    return rd;
}

static int readFileChunk(uint8_t *out, size_t outLen, void *ctxPtr) {
    auto *ctx = static_cast<FileReadContext *>(ctxPtr);
    return (int)fread(out, 1, outLen, ctx->file);
}

static voidpf zlibAllocWithCaps(voidpf opaque, uInt items, uInt size) {
    (void)opaque;
    size_t bytes = (size_t)items * (size_t)size;
#if CONFIG_SPIRAM
    void *ptr = heap_caps_malloc_prefer(bytes, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    return ptr;
#else
    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#endif
}

static void zlibFreeWithCaps(voidpf opaque, voidpf address) {
    (void)opaque;
    if (address) heap_caps_free(address);
}

static bool inflateGzipStreamToOta(int (*readChunk)(uint8_t *, size_t, void *), void *readCtx,
                                   int progressInputTotal, int progressBase,
                                   int progressRange, bool validateFirmwareMagic,
                                   std::string &errorOut) {
    const size_t IN_CHUNK = 4096;
    const size_t OUT_CHUNK = 4096;
    uint8_t *inbuf = (uint8_t *)malloc(IN_CHUNK);
    uint8_t *outbuf = (uint8_t *)malloc(OUT_CHUNK);
    if (!inbuf || !outbuf) {
        free(inbuf);
        free(outbuf);
        errorOut = "malloc failed for zlib buffers";
        return false;
    }

    z_stream strm = {};
    strm.zalloc = zlibAllocWithCaps;
    strm.zfree = zlibFreeWithCaps;
    strm.opaque = Z_NULL;
    int zret = inflateInit2(&strm, 16 + MAX_WBITS);
    if (zret != Z_OK) {
        free(inbuf);
        free(outbuf);
        errorOut = "zlib init failed (ret=" + std::to_string(zret) + ")";
        return false;
    }

    bool ok = true;
    bool done = false;
    bool firstOutputChunk = true;
    int lastPct = progressBase - 1;

    while (!done) {
        int rd = readChunk(inbuf, IN_CHUNK, readCtx);
        if (rd < 0) {
            ok = false;
            errorOut = "gzip source read failed";
            break;
        }
        if (rd == 0) {
            ok = false;
            if (errorOut.empty()) errorOut = "gzip stream ended unexpectedly";
            break;
        }

        strm.next_in = inbuf;
        strm.avail_in = (uInt)rd;

        while (strm.avail_in > 0) {
            strm.next_out = outbuf;
            strm.avail_out = (uInt)OUT_CHUNK;

            zret = inflate(&strm, Z_NO_FLUSH);
            if (zret == Z_BUF_ERROR) {
                if (strm.avail_in == 0) break;
            } else if (zret != Z_OK && zret != Z_STREAM_END) {
                ok = false;
                uint32_t freeHeap = esp_get_free_heap_size();
                uint32_t largestBlk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
                errorOut = "gzip decompress error " + std::to_string(zret) +
                           " free_heap=" + std::to_string(freeHeap) +
                           " largest_blk=" + std::to_string(largestBlk);
                break;
            }

            size_t produced = OUT_CHUNK - (size_t)strm.avail_out;
            if (produced > 0) {
                if (validateFirmwareMagic && firstOutputChunk && outbuf[0] != 0xE9) {
                    ok = false;
                    errorOut = "Invalid firmware magic after decompression";
                    ESP_LOGE(TAG, "Invalid decompressed magic: expected 0xE9, saw 0x%02X", outbuf[0]);
                    break;
                }
                firstOutputChunk = false;

                if (!gzWriteCallback(outbuf, produced)) {
                    ok = false;
                    if (errorOut.empty()) errorOut = "OTA write failed";
                    break;
                }
            }

            if (progressInputTotal > 0 && progressRange > 0) {
                int pct = progressBase + (int)((long long)strm.total_in * progressRange / progressInputTotal);
                int maxPct = progressBase + progressRange;
                if (pct > maxPct) pct = maxPct;
                if (pct != lastPct) {
                    broadcastOtaStatus("progress", "", pct);
                    lastPct = pct;
                }
            }

            if (zret == Z_STREAM_END) {
                done = true;
                break;
            }
        }

        if (!ok) break;
    }

    inflateEnd(&strm);
    free(inbuf);
    free(outbuf);
    return ok && done;
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
        if (isNonRetriableHttpStatus(code)) {
            ESP_LOGW(TAG, "Resolve URL got HTTP %d, skipping retries", code);
            return "";
        }
        if (code == 200) return current;
        if ((code==301||code==302||code==303||code==307||code==308) && !ctx.location.empty()) {
            ESP_LOGI(TAG, "Redirect %d → %s", code, ctx.location.c_str());
            current = ctx.location;
        } else break;
    }
    return current;
}

// ── Remote gz OTA: streaming HTTP → zlib → flash ──────────────────────────────
bool performGzOtaUpdate(std::string &errorOut) {
    otaInProgress = true;
    int64_t otaStartMs = esp_timer_get_time() / 1000LL;
    s_ota_started = false;
    s_ota_handle  = 0;
    s_ota_part    = nullptr;
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
    if (resolvedUrl.empty()) {
        errorOut = "Firmware URL not found (HTTP 404)";
        otaInProgress = false;
        broadcastOtaStatus("error", errorOut, -1);
        return false;
    }
    ESP_LOGI(TAG, "Resolved URL:  %s", resolvedUrl.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url               = resolvedUrl.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.method            = HTTP_METHOD_GET;
    cfg.timeout_ms        = 10000;
    cfg.buffer_size       = 16384;
    cfg.buffer_size_tx    = 4096;  // CDN redirect URLs can exceed 1500 chars
    cfg.keep_alive_enable = true;

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
    int64_t streamStartMs = esp_timer_get_time() / 1000LL;
    int code     = esp_http_client_get_status_code(s_stream_client);
    if (code != 200) {  // clen may be -1 for chunked; that is acceptable
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false;
        if (isNonRetriableHttpStatus(code))
            errorOut = "HTTP 404 (not retriable)";
        else
            errorOut = "HTTP error " + std::to_string(code);
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    // clen > 0: use as decompressed-size estimate; -1 (chunked): disable % display
    s_ota_total_est = (clen > 0) ? (int)((int64_t)clen * 3 / 2) : 0;

    // Pre-read first bytes so we can validate this is actually gzip.
    uint8_t prefetchBuf[8192];
    int firstRead = esp_http_client_read(s_stream_client, (char *)prefetchBuf, sizeof(prefetchBuf));
    if (firstRead <= 0) {
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "empty HTTP body";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    if (firstRead < 2 || prefetchBuf[0] != 0x1F || prefetchBuf[1] != 0x8B) {
        char hex[32] = {0};
        snprintf(hex, sizeof(hex), "%02X %02X %02X %02X", prefetchBuf[0],
                 firstRead > 1 ? prefetchBuf[1] : 0,
                 firstRead > 2 ? prefetchBuf[2] : 0,
                 firstRead > 3 ? prefetchBuf[3] : 0);
        ESP_LOGE(TAG, "Remote payload is not gzip (first bytes: %s)", hex);
        esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;
        otaInProgress = false; errorOut = "Remote payload is not gzip";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    HttpReadContext readCtx = {
        .client = s_stream_client,
        .prefetch = prefetchBuf,
        .prefetch_len = firstRead,
        .prefetch_pos = 0,
    };
    bool ok = inflateGzipStreamToOta(readHttpChunk, &readCtx,
                                     0, 0, 0, true, errorOut);
    esp_http_client_close(s_stream_client); esp_http_client_cleanup(s_stream_client); s_stream_client = nullptr;

    if (!ok || !s_ota_started) {
        if (s_ota_started) esp_ota_abort(s_ota_handle);
        s_ota_started = false;
        s_ota_handle  = 0;
        s_ota_part    = nullptr;
        otaInProgress = false;
        if (errorOut.empty()) errorOut = "gz decompression/flash failed";
        int64_t failMs = esp_timer_get_time() / 1000LL;
        ESP_LOGW(TAG, "OTA failed after %lld ms (written=%u bytes)",
                 (long long)(failMs - otaStartMs), (unsigned)s_ota_written);
        broadcastOtaStatus("error", errorOut, -1);
        return false;
    }

    if (esp_ota_end(s_ota_handle) != ESP_OK) {
        s_ota_started = false;
        s_ota_handle  = 0;
        s_ota_part    = nullptr;
        otaInProgress = false; errorOut = "esp_ota_end failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }
    if (esp_ota_set_boot_partition(s_ota_part) != ESP_OK) {
        s_ota_started = false;
        s_ota_handle  = 0;
        s_ota_part    = nullptr;
        otaInProgress = false; errorOut = "set_boot_partition failed";
        broadcastOtaStatus("error", errorOut, -1); return false;
    }

    s_ota_started = false;
    s_ota_handle  = 0;
    s_ota_part    = nullptr;
    otaInProgress = false;
    int64_t doneMs = esp_timer_get_time() / 1000LL;
    int64_t streamMs = doneMs - streamStartMs;
    int64_t totalMs = doneMs - otaStartMs;
    ESP_LOGI(TAG, "OTA timing: stream+inflate=%lld ms total=%lld ms written=%u bytes",
             (long long)streamMs, (long long)totalMs, (unsigned)s_ota_written);
    if (streamMs > 0) {
        uint32_t kbps = (uint32_t)(((uint64_t)s_ota_written * 1000ULL) / ((uint64_t)streamMs * 1024ULL));
        ESP_LOGI(TAG, "OTA effective write throughput: %u KiB/s", (unsigned)kbps);
    }
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

        FILE *gzFile = fopen(tmpPath, "rb");
        if (!gzFile) {
            remove(tmpPath); esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "tmpfile reopen failed");
            otaInProgress = false; return ESP_FAIL;
        }
        // Phase 2: decompress progress 50-99% (if upload phase known) or 0-99%
        // Progress is based on compressed input bytes consumed.
        const int progressBase = (total > 0) ? 50 : 0;
        const int progressRange = 99 - progressBase;  // 49 or 99
        broadcastOtaStatus("progress", "", progressBase);

        s_ota_started = true;
        s_ota_handle = ota_handle;
        s_ota_part = ota_part;
        s_ota_written = 0;
        s_ota_total_est = 0;

        FileReadContext readCtx = { .file = gzFile };
        std::string inflateError;
        bool ok = inflateGzipStreamToOta(readFileChunk, &readCtx,
                                         received, progressBase, progressRange,
                                         false, inflateError);

        fclose(gzFile);
        remove(tmpPath);

        if (!ok) {
            if (!inflateError.empty()) ESP_LOGE(TAG, "%s", inflateError.c_str());
            esp_ota_abort(ota_handle);
            s_ota_started = false;
            s_ota_handle = 0;
            s_ota_part = nullptr;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "gz decompress/flash failed");
            otaInProgress = false; return ESP_FAIL;
        }
        if (esp_ota_end(ota_handle) != ESP_OK) {
            s_ota_started = false;
            s_ota_handle = 0;
            s_ota_part = nullptr;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end failed");
            otaInProgress = false; return ESP_FAIL;
        }
        if (esp_ota_set_boot_partition(ota_part) != ESP_OK) {
            s_ota_started = false;
            s_ota_handle = 0;
            s_ota_part = nullptr;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot failed");
            otaInProgress = false; return ESP_FAIL;
        }
        s_ota_started = false;
        s_ota_handle = 0;
        s_ota_part = nullptr;
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
