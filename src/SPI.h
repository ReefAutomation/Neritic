/**
 * Minimal SPI.h compatibility stub for ESP-IDF builds.
 * Provides minimal SPI symbols for compatibility without linking real SPI.
 * SPI-based LED methods (DotStar) are never instantiated in this project.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MSBFIRST 1
#define LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPISettings {
public:
  SPISettings(uint32_t, uint8_t, uint8_t) {}
  SPISettings() {}
};

class SPIClass {
public:
  void begin(int = -1, int = -1, int = -1, int = -1) {}
  void end() {}
  void beginTransaction(const SPISettings &) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t) { return 0; }
  void writeBytes(const uint8_t *, size_t) {}
};

extern SPIClass SPI;
