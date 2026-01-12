#pragma once
#include <memory>
#include <vector>
#include <stdint.h>
#include "debug.h"

// Abstract base class for all bus types
class Bus {
public:
    virtual ~Bus() {}
    virtual void begin() {}
    virtual void show() = 0;
    virtual void setPixelColor(uint16_t pix, uint32_t color) = 0;
    virtual void setBrightness(uint8_t bri) {}
    virtual uint32_t getPixelColor(uint16_t pix) const { return 0; }
    virtual uint16_t getLength() const = 0;
};

enum class BusNeoPixelType { SK6812, WS2812B_RGB, WS2812B_GRB };
class BusNeoPixel : public Bus {
public:
    BusNeoPixel(void* strip, uint16_t len, BusNeoPixelType type) : _strip(strip), _len(len), _type(type) {}
    void show() override;
    void setPixelColor(uint16_t pix, uint32_t color) override;
    uint16_t getLength() const override { return _len; }
    void* getStrip() const { return _strip; }
    BusNeoPixelType getType() const { return _type; }
protected:
    void* _strip;
    uint16_t _len;
    BusNeoPixelType _type;
};

// BusManager holds all buses and routes calls
class BusManager {
public:
    void turnOffLEDs();
    BusNeoPixel* getNeoPixelBus();
    void addBus(std::unique_ptr<Bus> bus) { buses.push_back(std::move(bus)); }
    void setupStrip(const String& type, const String& colorOrder, uint8_t pin, uint16_t count);
    void cleanupStrip();
    void show() { for (auto& bus : buses) bus->show(); }
    void setPixelColor(uint16_t pix, uint32_t color) {
        for (auto& bus : buses) {
            if (pix < bus->getLength()) {
                bus->setPixelColor(pix, color);
                break;
            } else {
                pix -= bus->getLength();
            }
        }
    }
    void setBrightness(uint8_t bri) { for (auto& bus : buses) bus->setBrightness(bri); }
    uint16_t totalLength() const {
        uint16_t sum = 0;
        for (const auto& bus : buses) sum += bus->getLength();
        return sum;
    }

    // Update pixel count for all buses (returns total)
    uint16_t updatePixelCount();
    uint16_t getPixelCount() const { return pixelCount; }
private:
    std::vector<std::unique_ptr<Bus>> buses;
    uint16_t pixelCount = 0;
};

// You can extend with BusPWM, BusNetwork, etc. as needed.
