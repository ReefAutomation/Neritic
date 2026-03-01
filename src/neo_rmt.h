#pragma once

#include <stdint.h>

/**
 * NeoRmtStrip — thin ESP-IDF 5.x RMT driver for WS2812B / SK6812 LEDs.
 *
 * Pixel bytes are stored in wire order:
 *  SK6812 (rgbw=true) : G-R-B-W  (4 bytes / pixel)
 *  WS2812B GRB        : G-R-B    (3 bytes / pixel)
 *  WS2812B RGB        : R-G-B    (3 bytes / pixel)
 *
 * Callers are responsible for placing bytes in the correct wire order
 * before calling SetPixelBytes().
 */
class NeoRmtStrip {
public:
  void LockFrameBuffer();
  void UnlockFrameBuffer();
  NeoRmtStrip(uint16_t count, uint8_t pin, bool rgbw, bool grbOrder);
  ~NeoRmtStrip();

  bool Begin();
  void Show(); // Only called by update task

  // Start the LED update task (call once after strip is created)
  void StartUpdateTask();
  static void led_update_task(void *param);
  // Signal the update task that a new frame is ready
  void SignalFrameReady();

  /** Write bytesPerPixel bytes starting at index. */
  void SetPixelBytes(uint16_t index, const uint8_t *bytes);
  /** Read bytesPerPixel bytes at index. */
  void GetPixelBytes(uint16_t index, uint8_t *bytes) const;

  uint16_t PixelCount() const { return _count; }
  uint8_t BytesPerPixel() const { return _bytesPerPixel; }

private:
  uint16_t _count;
  uint8_t _pin;
  bool _rgbw;
  bool _grbOrder;
  uint8_t _bytesPerPixel;
  uint8_t *_pixels;

  // Shadow buffer for frame updates
  uint8_t *_frameBuffer;

  // Task handle and notification
  void *_updateTaskHandle; // Actually TaskHandle_t

  // Actually led_strip_handle_t, kept opaque to avoid exposing component
  // headers
  void *_strip;

  // Mutex for thread safety
  void *_mutex; // Actually a SemaphoreHandle_t, but avoid including FreeRTOS in
                // header
};
