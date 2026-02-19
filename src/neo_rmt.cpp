#include "neo_rmt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_spi.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "neo_rmt";

enum class LedBackend {
  Rmt,
  Spi,
};

static LedBackend resolvePreferredBackend() {
#if defined(DEEPGLOW_LED_BACKEND_SPI)
  return LedBackend::Spi;
#elif defined(DEEPGLOW_LED_BACKEND_RMT)
  return LedBackend::Rmt;
#elif CONFIG_IDF_TARGET_ESP32
  return LedBackend::Spi;
#elif CONFIG_IDF_TARGET_ESP32C6
  return LedBackend::Rmt;
#else
  return LedBackend::Rmt;
#endif
}

static bool isBackendForced() {
#if defined(DEEPGLOW_LED_BACKEND_SPI) || defined(DEEPGLOW_LED_BACKEND_RMT)
  return true;
#else
  return false;
#endif
}

static constexpr uint32_t kMinRmtMemBlockSymbols = 64;

static uint32_t resolveMemBlockSymbols() {
  uint32_t symbols = kMinRmtMemBlockSymbols;
#if CONFIG_IDF_TARGET_ESP32
  // Larger symbol buffer lowers ISR pressure and helps avoid TX underruns.
  if (symbols < 256) {
    symbols = 256;
  }
#endif
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

void NeoRmtStrip::LockFrameBuffer() {
  if (_mutex)
    xSemaphoreTake((SemaphoreHandle_t)_mutex, portMAX_DELAY);
}

void NeoRmtStrip::UnlockFrameBuffer() {
  if (_mutex)
    xSemaphoreGive((SemaphoreHandle_t)_mutex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / destructor
// ─────────────────────────────────────────────────────────────────────────────

NeoRmtStrip::NeoRmtStrip(uint16_t count, uint8_t pin, bool rgbw, bool grbOrder)
    : _count(count), _pin(pin), _rgbw(rgbw), _grbOrder(grbOrder),
      _bytesPerPixel(rgbw ? 4 : 3), _pixels(nullptr), _frameBuffer(nullptr),
      _updateTaskHandle(nullptr), _strip(nullptr), _mutex(nullptr) {
  _pixels = static_cast<uint8_t *>(calloc(count * _bytesPerPixel, 1));
  _frameBuffer = static_cast<uint8_t *>(calloc(count * _bytesPerPixel, 1));
  _mutex = xSemaphoreCreateMutex();
}

NeoRmtStrip::~NeoRmtStrip() {
  if (_strip) {
    led_strip_del(static_cast<led_strip_handle_t>(_strip));
    _strip = nullptr;
  }
  free(_pixels);
  _pixels = nullptr;
  free(_frameBuffer);
  _frameBuffer = nullptr;
  if (_mutex) {
    vSemaphoreDelete((SemaphoreHandle_t)_mutex);
    _mutex = nullptr;
  }
  // No need to delete task, it will exit on destruction
}

// ─────────────────────────────────────────────────────────────────────────────
// LED update task and signaling
// ─────────────────────────────────────────────────────────────────────────────

void NeoRmtStrip::StartUpdateTask() {
  if (_updateTaskHandle)
    return;
#if CONFIG_IDF_TARGET_ESP32
  xTaskCreatePinnedToCore(NeoRmtStrip::led_update_task, "led_update", 3072,
                          this, 9, (TaskHandle_t *)&_updateTaskHandle, 1);
#else
  xTaskCreate(NeoRmtStrip::led_update_task, "led_update", 3072, this, 9,
              (TaskHandle_t *)&_updateTaskHandle);
#endif
}

void NeoRmtStrip::SignalFrameReady() {
  if (_updateTaskHandle) {
    xTaskNotifyGive((TaskHandle_t)_updateTaskHandle);
  }
}

void NeoRmtStrip::led_update_task(void *param) {
  NeoRmtStrip *strip = static_cast<NeoRmtStrip *>(param);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
    }
    if (strip->_mutex)
      xSemaphoreTake((SemaphoreHandle_t)strip->_mutex, portMAX_DELAY);
    memcpy(strip->_pixels, strip->_frameBuffer,
           strip->_count * strip->_bytesPerPixel);
    if (strip->_mutex)
      xSemaphoreGive((SemaphoreHandle_t)strip->_mutex);
    strip->Show();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Begin — allocate & start the RMT TX channel
// ─────────────────────────────────────────────────────────────────────────────

bool NeoRmtStrip::Begin() {
  ESP_LOGI(TAG, "Begin: pin=%d count=%d rgbw=%d grb=%d", _pin, _count,
           (int)_rgbw, (int)_grbOrder);

  if (!_pixels) {
    ESP_LOGE(TAG, "pixel buffer allocation failed");
    return false;
  }
  led_strip_config_t strip_config = {};
  strip_config.strip_gpio_num = _pin;
  strip_config.max_leds = _count;
  strip_config.led_model = _rgbw ? LED_MODEL_SK6812 : LED_MODEL_WS2812;
  if (_rgbw) {
    strip_config.color_component_format =
        _grbOrder ? LED_STRIP_COLOR_COMPONENT_FMT_GRBW
                  : LED_STRIP_COLOR_COMPONENT_FMT_RGBW;
  } else {
    strip_config.color_component_format =
        _grbOrder ? LED_STRIP_COLOR_COMPONENT_FMT_GRB
                  : LED_STRIP_COLOR_COMPONENT_FMT_RGB;
  }
  strip_config.flags.invert_out = false;

  led_strip_handle_t strip = nullptr;
  esp_err_t err = ESP_FAIL;
  const LedBackend preferred = resolvePreferredBackend();
  const bool forced = isBackendForced();

  auto try_rmt = [&]() {
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = resolveMemBlockSymbols();
    rmt_config.flags.with_dma = false;
    return led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
  };

  auto try_spi = [&]() {
    led_strip_spi_config_t spi_config = {};
    spi_config.clk_src = SPI_CLK_SRC_DEFAULT;
    spi_config.spi_bus = SPI2_HOST;
    spi_config.flags.with_dma = true;
    return led_strip_new_spi_device(&strip_config, &spi_config, &strip);
  };

  if (preferred == LedBackend::Spi) {
    err = try_spi();
    if (err != ESP_OK && !forced) {
      ESP_LOGW(TAG, "SPI backend init failed (%s), trying RMT",
               esp_err_to_name(err));
      err = try_rmt();
    }
  } else {
    err = try_rmt();
    if (err != ESP_OK && !forced) {
      ESP_LOGW(TAG, "RMT backend init failed (%s), trying SPI",
               esp_err_to_name(err));
      err = try_spi();
    }
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to create LED strip backend: %s",
             esp_err_to_name(err));
    return false;
  }
  _strip = strip;
  led_strip_clear(strip);

  ESP_LOGI(TAG, "LED strip ready: pin=%d count=%d model=%s", _pin, _count,
           _rgbw ? "SK6812" : "WS2812");
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Show — transmit pixel buffer, then hold low for reset pulse
// ─────────────────────────────────────────────────────────────────────────────

void NeoRmtStrip::Show() {
  if (!_strip || !_pixels)
    return;
  if (_mutex)
    xSemaphoreTake((SemaphoreHandle_t)_mutex, portMAX_DELAY);

  led_strip_handle_t strip = static_cast<led_strip_handle_t>(_strip);
  for (uint16_t i = 0; i < _count; ++i) {
    const uint8_t *pixel = _pixels + i * _bytesPerPixel;
    if (_rgbw) {
      const uint8_t g = pixel[0];
      const uint8_t r = pixel[1];
      const uint8_t b = pixel[2];
      const uint8_t w = pixel[3];
      led_strip_set_pixel_rgbw(strip, i, r, g, b, w);
    } else {
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      if (_grbOrder) {
        g = pixel[0];
        r = pixel[1];
        b = pixel[2];
      } else {
        r = pixel[0];
        g = pixel[1];
        b = pixel[2];
      }
      led_strip_set_pixel(strip, i, r, g, b);
    }
  }

  esp_err_t err = led_strip_refresh(strip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "led_strip_refresh failed: %s", esp_err_to_name(err));
  }

  if (_mutex)
    xSemaphoreGive((SemaphoreHandle_t)_mutex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel buffer access
// ─────────────────────────────────────────────────────────────────────────────

void NeoRmtStrip::SetPixelBytes(uint16_t index, const uint8_t *bytes) {
  // ESP_LOGW(TAG, "SetPixelBytes idx=%u bytes=%02x%02x%02x%02x task=%p
  // tick=%lu", index, bytes[0], bytes[1], bytes[2],
  // _bytesPerPixel==4?bytes[3]:0, xTaskGetCurrentTaskHandle(),
  // xTaskGetTickCount()); ESP_LOGW(TAG, "led_update_task: copying frame buffer
  // at tick=%lu", xTaskGetTickCount()); ESP_LOGW(TAG, "led_update_task: calling
  // Show() at tick=%lu", xTaskGetTickCount());
  if (index >= _count || !_frameBuffer)
    return;
  memcpy(_frameBuffer + index * _bytesPerPixel, bytes, _bytesPerPixel);
}

void NeoRmtStrip::GetPixelBytes(uint16_t index, uint8_t *bytes) const {
  if (index >= _count || !_pixels) {
    memset(bytes, 0, _bytesPerPixel);
    return;
  }
  if (_mutex)
    xSemaphoreTake((SemaphoreHandle_t)_mutex, portMAX_DELAY);
  memcpy(bytes, _pixels + index * _bytesPerPixel, _bytesPerPixel);
  if (_mutex)
    xSemaphoreGive((SemaphoreHandle_t)_mutex);
}
