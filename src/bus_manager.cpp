#include "bus_manager.h"
#include "colors.h"
#include "esp_log.h"
#include <strings.h>

#include "neo_rmt.h"

BusNeoPixel *BusManager::getLedBus() {
  for (const auto &bus : buses) {
    // Only use static_cast since we control the bus type
    BusNeoPixel *neo = static_cast<BusNeoPixel *>(bus.get());
    if (neo)
      return neo;
  }
  return nullptr;
}

bool BusManager::ledsReady = false;

void BusManager::beginFrame() {
  BusNeoPixel *neo = getLedBus();
  if (!neo || !neo->getStrip())
    return;
  auto *s = static_cast<NeoRmtStrip *>(neo->getStrip());
  s->LockFrameBuffer();
}

void BusManager::endFrame() {
  BusNeoPixel *neo = getLedBus();
  if (!neo || !neo->getStrip())
    return;
  auto *s = static_cast<NeoRmtStrip *>(neo->getStrip());
  s->UnlockFrameBuffer();
  s->SignalFrameReady();
}

void BusManager::turnOffLEDs() {
  BusNeoPixel *neo = getLedBus();
  if (!neo || !neo->getStrip())
    return;
  uint16_t count = neo->getLength();
  auto *s = static_cast<NeoRmtStrip *>(neo->getStrip());
  const uint8_t zeros[4] = {0, 0, 0, 0};
  s->LockFrameBuffer();
  for (uint16_t i = 0; i < count; i++)
    s->SetPixelBytes(i, zeros);
  s->UnlockFrameBuffer();
  s->SignalFrameReady();
  ledsReady = true;
}
void BusManager::setLedsReady(bool ready) { ledsReady = ready; }

void BusNeoPixel::show() {
  if (!_strip)
    return;

  static_cast<NeoRmtStrip *>(_strip)->SignalFrameReady();
}

// Update pixel count for all buses (returns total)
uint16_t BusManager::updatePixelCount() {
  uint16_t total = 0;
  for (const auto &bus : buses) {
    total += bus->getLength();
  }
  pixelCount = total;
  return pixelCount;
}

// Wire orders:
//   SK6812      → G-R-B-W  (4 bytes)
//   WS2812B GRB → G-R-B    (3 bytes)
//   WS2812B RGB → R-G-B    (3 bytes)
void BusNeoPixel::setPixelColor(uint16_t pix, uint32_t color) {
  if (!_strip || !BusManager::ledsReady)
    return;
  uint8_t r, g, b, w;

  auto *s = static_cast<NeoRmtStrip *>(_strip);
  if (_type == BusNeoPixelType::SK6812) {
    unpack_rgbw(color, r, g, b, w);
    const uint8_t bytes[4] = {g, r, b, w};
    s->SetPixelBytes(pix, bytes);
  } else if (_type == BusNeoPixelType::WS2812B_GRB) {
    unpack_rgb(color, r, g, b);
    const uint8_t bytes[3] = {g, r, b};
    s->SetPixelBytes(pix, bytes);
  } else {
    unpack_rgb(color, r, g, b);
    const uint8_t bytes[3] = {r, g, b};
    s->SetPixelBytes(pix, bytes);
  }
}

uint32_t BusNeoPixel::getPixelColor(uint16_t pix) const {
  if (!_strip)
    return 0;

  auto *s = static_cast<NeoRmtStrip *>(_strip);
  uint8_t bytes[4] = {0, 0, 0, 0};
  s->GetPixelBytes(pix, bytes);
  if (_type == BusNeoPixelType::SK6812) {
    // Stored as [g,r,b,w] → pack_rgbw(r,g,b,w)
    return pack_rgbw(bytes[1], bytes[0], bytes[2], bytes[3]);
  } else if (_type == BusNeoPixelType::WS2812B_GRB) {
    // Stored as [g,r,b] → pack_rgb(r,g,b)
    return pack_rgb(bytes[1], bytes[0], bytes[2]);
  } else {
    // Stored as [r,g,b]
    return pack_rgb(bytes[0], bytes[1], bytes[2]);
  }
}

void BusManager::cleanupStrip() {
  if (!buses.empty()) {
    BusNeoPixel *neo = static_cast<BusNeoPixel *>(buses.front().get());
    if (neo) {
      void *s = neo->getStrip();
      delete static_cast<NeoRmtStrip *>(s);
    }
    buses.clear();
  }
}

void BusManager::setupStrip(const std::string &type,
                            const std::string &colorOrder, uint8_t pin,
                            uint16_t count) {
  cleanupStrip();

  BusNeoPixelType ledType = (strcasecmp(type.c_str(), "SK6812") == 0)
                                ? BusNeoPixelType::SK6812
                                : (strcasecmp(colorOrder.c_str(), "RGB") == 0
                                       ? BusNeoPixelType::WS2812B_RGB
                                       : BusNeoPixelType::WS2812B_GRB);

  bool rgbw = (ledType == BusNeoPixelType::SK6812);
  bool grbOrder = (ledType == BusNeoPixelType::SK6812 ||
                   ledType == BusNeoPixelType::WS2812B_GRB);
  ESP_LOGI("bus", "setupStrip: using NeoRmtStrip pin=%d count=%d rgbw=%d", pin,
           count, (int)rgbw);
  auto *s = new NeoRmtStrip(count, pin, rgbw, grbOrder);
  if (!s->Begin()) {
    delete s;
    return;
  }
  s->StartUpdateTask();
  s->SignalFrameReady();
  addBus(std::unique_ptr<BusNeoPixel>(new BusNeoPixel(s, count, ledType)));
}
