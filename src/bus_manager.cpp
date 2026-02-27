#include "bus_manager.h"
#include "colors.h"
#include <strings.h>

#ifdef ESP_PLATFORM
#include "neo_rmt.h"
#else
#include <NeoPixelBus.h>
#endif

BusNeoPixel *BusManager::getNeoPixelBus() {
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
#ifdef ESP_PLATFORM
  BusNeoPixel *neo = getNeoPixelBus();
  if (!neo || !neo->getStrip())
    return;
  auto *s = static_cast<NeoRmtStrip *>(neo->getStrip());
  s->LockFrameBuffer();
#endif
}

void BusManager::endFrame() {
#ifdef ESP_PLATFORM
  BusNeoPixel *neo = getNeoPixelBus();
  if (!neo || !neo->getStrip())
    return;
  auto *s = static_cast<NeoRmtStrip *>(neo->getStrip());
  s->UnlockFrameBuffer();
  s->SignalFrameReady();
#else
  show();
#endif
}

void BusManager::turnOffLEDs() {
  BusNeoPixel *neo = getNeoPixelBus();
  if (!neo || !neo->getStrip())
    return;
  uint16_t count = neo->getLength();
  if (!ledsReady) {
    // Only clear LEDs at boot, before NTP/timer checks
    #ifdef ESP_PLATFORM
    auto *s = static_cast<NeoRmtStrip *>(neo->getStrip());
    const uint8_t zeros[4] = {0, 0, 0, 0};
    s->LockFrameBuffer();
    for (uint16_t i = 0; i < count; i++)
      s->SetPixelBytes(i, zeros);
    s->UnlockFrameBuffer();
    s->SignalFrameReady();
    #else
    if (neo->getType() == BusNeoPixelType::SK6812) {
      auto *s = (NeoPixelBus<NeoRgbwFeature, NeoSk6812Method> *)neo->getStrip();
      RgbwColor off(0, 0, 0, 0);
      for (uint16_t i = 0; i < count; i++)
        s->SetPixelColor(i, off);
      s->Show();
    } else {
      auto *s = (NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod> *)neo->getStrip();
      RgbColor off(0, 0, 0);
      for (uint16_t i = 0; i < count; i++)
        s->SetPixelColor(i, off);
      s->Show();
    }
    #endif
    ledsReady = true;
  }
}
void BusManager::setLedsReady(bool ready) {
  ledsReady = ready;
}

void BusNeoPixel::show() {
  if (!_strip) return;

#ifdef ESP_PLATFORM
  static_cast<NeoRmtStrip *>(_strip)->SignalFrameReady();
#else
  switch (_type) {
  case BusNeoPixelType::SK6812:
    static_cast<NeoPixelBus<NeoRgbwFeature, NeoSk6812Method> *>(_strip)->Show();
    break;
  case BusNeoPixelType::WS2812B_RGB:
    static_cast<NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod> *>(_strip)->Show();
    break;
  case BusNeoPixelType::WS2812B_GRB:
    static_cast<NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> *>(_strip)->Show();
    break;
  }
#endif
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
  if (!_strip || !BusManager::ledsReady) return;
  uint8_t r, g, b, w;

#ifdef ESP_PLATFORM
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
#else
  if (_type == BusNeoPixelType::SK6812) {
    unpack_rgbw(color, r, g, b, w);
  } else {
    unpack_rgb(color, r, g, b);
    w = 0;
  }
  switch (_type) {
  case BusNeoPixelType::SK6812: {
    auto *s = static_cast<NeoPixelBus<NeoRgbwFeature, NeoSk6812Method> *>(_strip);
    s->SetPixelColor(pix, RgbwColor(g, r, b, w));
    break;
  }
  case BusNeoPixelType::WS2812B_RGB: {
    auto *s = static_cast<NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod> *>(_strip);
    s->SetPixelColor(pix, RgbColor(r, g, b));
    break;
  }
  case BusNeoPixelType::WS2812B_GRB: {
    auto *s = static_cast<NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> *>(_strip);
    s->SetPixelColor(pix, RgbColor(r, g, b));
    break;
  }
  }
#endif
}

uint32_t BusNeoPixel::getPixelColor(uint16_t pix) const {
  if (!_strip) return 0;

#ifdef ESP_PLATFORM
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
#else
  switch (_type) {
  case BusNeoPixelType::SK6812: {
    auto *s = static_cast<NeoPixelBus<NeoRgbwFeature, NeoSk6812Method> *>(_strip);
    RgbwColor c = s->GetPixelColor(pix);
    return pack_rgbw(c.G, c.R, c.B, c.W);
  }
  case BusNeoPixelType::WS2812B_RGB: {
    auto *s = static_cast<NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod> *>(_strip);
    RgbColor c = s->GetPixelColor(pix);
    return pack_rgb(c.R, c.G, c.B);
  }
  case BusNeoPixelType::WS2812B_GRB: {
    auto *s = static_cast<NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> *>(_strip);
    RgbColor c = s->GetPixelColor(pix);
    return pack_rgb(c.R, c.G, c.B);
  }
  }
  return 0;
#endif
}

void BusManager::cleanupStrip() {
  if (!buses.empty()) {
    BusNeoPixel *neo = static_cast<BusNeoPixel *>(buses.front().get());
    if (neo) {
      void *s = neo->getStrip();
#ifdef ESP_PLATFORM
      delete static_cast<NeoRmtStrip *>(s);
#else
      BusNeoPixelType t = neo->getType();
      if (t == BusNeoPixelType::SK6812)
        delete (NeoPixelBus<NeoRgbwFeature, NeoSk6812Method> *)s;
      else if (t == BusNeoPixelType::WS2812B_RGB)
        delete (NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod> *)s;
      else
        delete (NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> *)s;
#endif
    }
    buses.clear();
  }
}

void BusManager::setupStrip(const std::string &type, const std::string &colorOrder,
                            uint8_t pin, uint16_t count) {
  cleanupStrip();

  BusNeoPixelType ledType =
      (strcasecmp(type.c_str(), "SK6812") == 0)
          ? BusNeoPixelType::SK6812
          : (strcasecmp(colorOrder.c_str(), "RGB") == 0
                 ? BusNeoPixelType::WS2812B_RGB
                 : BusNeoPixelType::WS2812B_GRB);

#ifdef ESP_PLATFORM
  bool rgbw = (ledType == BusNeoPixelType::SK6812);
  bool grbOrder = (ledType == BusNeoPixelType::SK6812 || ledType == BusNeoPixelType::WS2812B_GRB);
  ESP_LOGI("bus", "setupStrip: using NeoRmtStrip pin=%d count=%d rgbw=%d", pin, count, (int)rgbw);
  auto *s = new NeoRmtStrip(count, pin, rgbw, grbOrder);
  if (!s->Begin()) {
    delete s;
    return;
  }
  s->StartUpdateTask();
  s->SignalFrameReady();
  addBus(std::unique_ptr<BusNeoPixel>(new BusNeoPixel(s, count, ledType)));

#else
  void *strip = nullptr;
  if (ledType == BusNeoPixelType::SK6812) {
    auto *s = new NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>(count, pin);
    s->Begin(); s->Show(); strip = s;
  } else if (ledType == BusNeoPixelType::WS2812B_RGB) {
    auto *s = new NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>(count, pin);
    s->Begin(); s->Show(); strip = s;
  } else {
    auto *s = new NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>(count, pin);
    s->Begin(); s->Show(); strip = s;
  }
  addBus(std::unique_ptr<BusNeoPixel>(new BusNeoPixel(strip, count, ledType)));
#endif
}
