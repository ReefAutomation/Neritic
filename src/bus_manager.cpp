#include <NeoPixelBus.h>
#include "bus_manager.h"
#include "colors.h"

BusNeoPixel* BusManager::getNeoPixelBus() {
    for (const auto& bus : buses) {
        // Only use static_cast since we control the bus type
        BusNeoPixel* neo = static_cast<BusNeoPixel*>(bus.get());
        if (neo) return neo;
    }
    return nullptr;
}

void BusManager::turnOffLEDs() {
    BusNeoPixel* neo = getNeoPixelBus();
    if (!neo || !neo->getStrip()) return;
    if (neo->getType() == BusNeoPixelType::SK6812) {
        auto* s = (NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*)neo->getStrip();
        RgbwColor off(0, 0, 0, 0);
        for (uint16_t i = 0; i < s->PixelCount(); i++) {
            s->SetPixelColor(i, off);
        }
        s->Show();
    } else {
        auto* s = (NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*)neo->getStrip();
        RgbColor off(0, 0, 0);
        for (uint16_t i = 0; i < s->PixelCount(); i++) {
            s->SetPixelColor(i, off);
        }
        s->Show();
    }
}

// Example implementation for NeoPixelBus wrapper
void BusNeoPixel::show() {
    if (!_strip) {
        return;
    }
    switch (_type) {
        case BusNeoPixelType::SK6812: {
            auto* s = static_cast<NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*>(_strip);
            s->Show();
            break;
        }
        case BusNeoPixelType::WS2812B_RGB: {
            auto* s = static_cast<NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*>(_strip);
            s->Show();
            break;
        }
        case BusNeoPixelType::WS2812B_GRB: {
            auto* s = static_cast<NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>*>(_strip);
            s->Show();
            break;
        }
    }
}

// Update pixel count for all buses (returns total)
uint16_t BusManager::updatePixelCount() {
    uint16_t total = 0;
    for (const auto& bus : buses) {
        total += bus->getLength();
    }
    pixelCount = total;
    return pixelCount;
}

void BusNeoPixel::setPixelColor(uint16_t pix, uint32_t color) {
    if (!_strip) {
        return;
    }
    uint8_t r, g, b, w;
    if (_type == BusNeoPixelType::SK6812) {
        unpack_rgbw(color, r, g, b, w);
    } else {
        unpack_rgb(color, r, g, b);
        w = 0;
    }
    switch (_type) {
        case BusNeoPixelType::SK6812: {
            auto* s = static_cast<NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*>(_strip);
            s->SetPixelColor(pix, RgbwColor(g, r, b, w)); // GRBW order: swap r and g
            break;
        }
        case BusNeoPixelType::WS2812B_RGB: {
            auto* s = static_cast<NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*>(_strip);
            s->SetPixelColor(pix, RgbColor(r, g, b));
            break;
        }
        case BusNeoPixelType::WS2812B_GRB: {
            auto* s = static_cast<NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>*>(_strip);
            s->SetPixelColor(pix, RgbColor(r, g, b));
            break;
        }
    }
}

// Return the color of a pixel from the underlying NeoPixelBus
uint32_t BusNeoPixel::getPixelColor(uint16_t pix) const {
    if (!_strip) return 0;
    switch (_type) {
        case BusNeoPixelType::SK6812: {
            auto* s = static_cast<NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*>(_strip);
            RgbwColor c = s->GetPixelColor(pix);
            // Convert GRBW to RGBW (swap r/g)
            return pack_rgbw(c.G, c.R, c.B, c.W);
        }
        case BusNeoPixelType::WS2812B_RGB: {
            auto* s = static_cast<NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*>(_strip);
            RgbColor c = s->GetPixelColor(pix);
            return pack_rgb(c.R, c.G, c.B);
        }
        case BusNeoPixelType::WS2812B_GRB: {
            auto* s = static_cast<NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>*>(_strip);
            RgbColor c = s->GetPixelColor(pix);
            return pack_rgb(c.R, c.G, c.B);
        }
    }
    return 0;
}

void BusManager::cleanupStrip() {
    if (!buses.empty()) {
        BusNeoPixel* neo = static_cast<BusNeoPixel*>(buses.front().get());
        if (neo) {
            void* strip = neo->getStrip();
            BusNeoPixelType prevType = neo->getType();
            if (prevType == BusNeoPixelType::SK6812) {
                delete (NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*)strip;
            } else if (prevType == BusNeoPixelType::WS2812B_RGB) {
                delete (NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*)strip;
            } else {
                delete (NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>*)strip;
            }
        }
        buses.clear();
    }
}

void BusManager::setupStrip(const String& type, const String& colorOrder, uint8_t pin, uint16_t count) {
    cleanupStrip();
    void* strip = nullptr;
    BusNeoPixelType ledType = type.equalsIgnoreCase("SK6812") ? BusNeoPixelType::SK6812 : (colorOrder.equalsIgnoreCase("RGB") ? BusNeoPixelType::WS2812B_RGB : BusNeoPixelType::WS2812B_GRB);
    if (ledType == BusNeoPixelType::SK6812) {
        strip = new NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>(count, pin);
        ((NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*)strip)->Begin();
        ((NeoPixelBus<NeoRgbwFeature, NeoSk6812Method>*)strip)->Show();
        addBus(std::unique_ptr<BusNeoPixel>(new BusNeoPixel(strip, count, BusNeoPixelType::SK6812)));
    } else if (ledType == BusNeoPixelType::WS2812B_RGB) {
        strip = new NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>(count, pin);
        ((NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*)strip)->Begin();
        ((NeoPixelBus<NeoRgbFeature, NeoWs2812xMethod>*)strip)->Show();
        addBus(std::unique_ptr<BusNeoPixel>(new BusNeoPixel(strip, count, BusNeoPixelType::WS2812B_RGB)));
    } else {
        strip = new NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>(count, pin);
        ((NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>*)strip)->Begin();
        ((NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod>*)strip)->Show();
        addBus(std::unique_ptr<BusNeoPixel>(new BusNeoPixel(strip, count, BusNeoPixelType::WS2812B_GRB)));
    }
}

// You can add more bus types (PWM, Network, etc.) as needed

// Example usage in main.cpp:
// BusManager busManager;
// busManager.addBus(std::make_unique<BusNeoPixel>(strip, pixelCount));
// busManager.setPixelColor(0, 0xFF0000); // Set first pixel to red
// busManager.show();
