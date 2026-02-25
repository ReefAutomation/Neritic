#ifdef ESP_PLATFORM

#include "neo_rmt.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

static const char *TAG = "neo_rmt";

// 10 MHz clock → 100 ns per tick
#define RMT_LED_RESOLUTION_HZ 10000000UL

static constexpr uint32_t kMinRmtMemBlockSymbols = 64;

static uint32_t resolveMemBlockSymbols()
{
    uint32_t symbols = kMinRmtMemBlockSymbols;
#ifdef SOC_RMT_MEM_WORDS_PER_CHANNEL
    if (SOC_RMT_MEM_WORDS_PER_CHANNEL > symbols) {
        symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    }
#endif
    if (symbols & 1U) {
        symbols += 1U;
    }
    return symbols;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / destructor
// ─────────────────────────────────────────────────────────────────────────────

NeoRmtStrip::NeoRmtStrip(uint16_t count, uint8_t pin, bool rgbw)
    : _count(count), _pin(pin), _rgbw(rgbw),
      _bytesPerPixel(rgbw ? 4 : 3),
      _pixels(nullptr), _chan(nullptr), _bytesEncoder(nullptr)
{
    _pixels = static_cast<uint8_t *>(calloc(count * _bytesPerPixel, 1));
}

NeoRmtStrip::~NeoRmtStrip()
{
    if (_chan) {
        rmt_disable(_chan);
        rmt_del_channel(_chan);
        _chan = nullptr;
    }
    if (_bytesEncoder) {
        rmt_del_encoder(_bytesEncoder);
        _bytesEncoder = nullptr;
    }
    free(_pixels);
    _pixels = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Begin — allocate & start the RMT TX channel
// ─────────────────────────────────────────────────────────────────────────────

bool NeoRmtStrip::Begin()
{
    ESP_LOGI(TAG, "Begin: pin=%d count=%d rgbw=%d", _pin, _count, (int)_rgbw);

    if (!_pixels) {
        ESP_LOGE(TAG, "pixel buffer allocation failed");
        return false;
    }
    ESP_LOGI(TAG, "Begin: pixel buf ok, calling rmt_new_tx_channel");

    // ── TX channel ────────────────────────────────────────────────────────
    rmt_tx_channel_config_t tx_chan_cfg = {};
    tx_chan_cfg.gpio_num          = static_cast<gpio_num_t>(_pin);
    tx_chan_cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
    tx_chan_cfg.resolution_hz     = RMT_LED_RESOLUTION_HZ;
    tx_chan_cfg.mem_block_symbols = resolveMemBlockSymbols();
    tx_chan_cfg.trans_queue_depth = 4;

    esp_err_t err = rmt_new_tx_channel(&tx_chan_cfg, &_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Begin: tx channel ok, calling rmt_new_bytes_encoder");

    // ── Bytes encoder ─────────────────────────────────────────────────────
    // Timings @ 10 Mticks/s (100 ns / tick):
    //
    //   SK6812  T0H=300ns T0L=900ns  T1H=600ns T1L=600ns  Reset ≥ 80 µs
    //   WS2812B T0H=400ns T0L=850ns  T1H=800ns T1L=450ns  Reset ≥ 50 µs
    //
    rmt_bytes_encoder_config_t bytes_enc_cfg = {};
    if (_rgbw) {
        // SK6812
        bytes_enc_cfg.bit0.level0    = 1;
        bytes_enc_cfg.bit0.duration0 = 3;   // 300 ns
        bytes_enc_cfg.bit0.level1    = 0;
        bytes_enc_cfg.bit0.duration1 = 9;   // 900 ns
        bytes_enc_cfg.bit1.level0    = 1;
        bytes_enc_cfg.bit1.duration0 = 6;   // 600 ns
        bytes_enc_cfg.bit1.level1    = 0;
        bytes_enc_cfg.bit1.duration1 = 6;   // 600 ns
    } else {
        // WS2812B
        bytes_enc_cfg.bit0.level0    = 1;
        bytes_enc_cfg.bit0.duration0 = 4;   // 400 ns
        bytes_enc_cfg.bit0.level1    = 0;
        bytes_enc_cfg.bit0.duration1 = 8;   // 800 ns
        bytes_enc_cfg.bit1.level0    = 1;
        bytes_enc_cfg.bit1.duration0 = 8;   // 800 ns
        bytes_enc_cfg.bit1.level1    = 0;
        bytes_enc_cfg.bit1.duration1 = 4;   // 400 ns
    }
    bytes_enc_cfg.flags.msb_first = 1;

    err = rmt_new_bytes_encoder(&bytes_enc_cfg, &_bytesEncoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Begin: encoder ok, calling rmt_enable");

    // ── Enable ────────────────────────────────────────────────────────────
    err = rmt_enable(_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "RMT TX ready: pin=%d count=%d %s",
             _pin, _count, _rgbw ? "RGBW" : "RGB");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Show — transmit pixel buffer, then hold low for reset pulse
// ─────────────────────────────────────────────────────────────────────────────

void NeoRmtStrip::Show()
{
    if (!_chan || !_bytesEncoder || !_pixels) return;

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;

    esp_err_t err = rmt_transmit(_chan, _bytesEncoder,
                                 _pixels, _count * _bytesPerPixel,
                                 &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        return;
    }

    rmt_tx_wait_all_done(_chan, portMAX_DELAY);

    // Reset pulse: hold data line low for ≥ 80 µs (SK6812) / ≥ 50 µs (WS2812B)
    esp_rom_delay_us(100);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel buffer access
// ─────────────────────────────────────────────────────────────────────────────

void NeoRmtStrip::SetPixelBytes(uint16_t index, const uint8_t *bytes)
{
    if (index >= _count || !_pixels) return;
    memcpy(_pixels + index * _bytesPerPixel, bytes, _bytesPerPixel);
}

void NeoRmtStrip::GetPixelBytes(uint16_t index, uint8_t *bytes) const
{
    if (index >= _count || !_pixels) {
        memset(bytes, 0, _bytesPerPixel);
        return;
    }
    memcpy(bytes, _pixels + index * _bytesPerPixel, _bytesPerPixel);
}

#endif // ESP_PLATFORM
