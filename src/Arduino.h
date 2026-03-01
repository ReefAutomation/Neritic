/**
 * Minimal Arduino compatibility header for ESP-IDF builds.
 * Provides just enough to compile NeoPixelBus (RMT methods only).
 */
#pragma once

#include <algorithm>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef unsigned char byte;
typedef unsigned int word;

#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef HALF_PI
#define HALF_PI (PI / 2.0f)
#endif
#ifndef TWO_PI
#define TWO_PI (PI * 2.0f)
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD (PI / 180.0f)
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG (180.0f / PI)
#endif
#ifndef EULER
#define EULER 2.718281828459045f
#endif

static inline unsigned long millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}
static inline unsigned long micros() {
  return (unsigned long)(esp_timer_get_time());
}
static inline void delay(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
static inline void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }
static inline void yield() { taskYIELD(); }

#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif
#ifndef sq
#define sq(x) ((x) * (x))
#endif
#ifndef _BV
#define _BV(b) (1UL << (b))
#endif
#define bitRead(v, b) (((v) >> (b)) & 0x01)
#define bitSet(v, b) ((v) |= _BV(b))
#define bitClear(v, b) ((v) &= ~_BV(b))
#define bitWrite(v, b, bv) ((bv) ? bitSet(v, b) : bitClear(v, b))
#define bit(b) _BV(b)
#define lowByte(w) ((uint8_t)((w) & 0xFF))
#define highByte(w) ((uint8_t)((w) >> 8))

#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
typedef const char *PGM_P;
typedef const char *PGM_VOID_P;
#define pgm_read_byte(a) (*((const uint8_t *)(a)))
#define pgm_read_word(a) (*((const uint16_t *)(a)))
#define pgm_read_dword(a) (*((const uint32_t *)(a)))
#define pgm_read_float(a) (*((const float *)(a)))
#define pgm_read_ptr(a) (*((const void **)(a)))
#define strncpy_P(d, s, n) strncpy(d, s, n)
#define strlen_P(s) strlen(s)
#define strcmp_P(a, b) strcmp(a, b)
#define strcasecmp_P(a, b) strcasecmp(a, b)
#define memcpy_P(d, s, n) memcpy(d, s, n)

#define OUTPUT 0x03
#define INPUT 0x01
#define INPUT_PULLUP 0x05
#define HIGH 1
#define LOW 0

static inline void pinMode(uint8_t pin, uint8_t mode) {
  gpio_config_t c = {};
  c.pin_bit_mask = (1ULL << pin);
  c.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
  c.pull_up_en =
      (mode == INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  c.pull_down_en = GPIO_PULLDOWN_DISABLE;
  c.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&c);
}
static inline void digitalWrite(uint8_t pin, uint8_t val) {
  gpio_set_level((gpio_num_t)pin, val);
}
static inline int digitalRead(uint8_t pin) {
  return gpio_get_level((gpio_num_t)pin);
}
static inline int analogRead(uint8_t) { return 0; }

#ifndef SCK
#define SCK 18
#endif
#ifndef MOSI
#define MOSI 19
#endif
#ifndef MISO
#define MISO 20
#endif
#ifndef SS
#define SS 21
#endif
#ifndef HSPI
#define HSPI 0
#endif

#define interrupts() portENABLE_INTERRUPTS()
#define noInterrupts() portDISABLE_INTERRUPTS()

class HardwareSerial {
public:
  void begin(unsigned long, uint32_t = 0) {}
  void end() {}
  void flush() {}
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  template <typename T> size_t print(T) { return 0; }
  template <typename T> size_t println(T) { return 0; }
  size_t print(int v, int) { return 0; }
  size_t print(unsigned v, int) { return 0; }
  size_t print(long v, int) { return 0; }
  size_t print(unsigned long v, int) { return 0; }
  size_t print(const char *s) { return s ? strlen(s) : 0; }
  size_t println(const char *s) { return (s ? strlen(s) : 0) + 2; }
  size_t println() { return 2; }
  size_t write(uint8_t) { return 1; }
  operator bool() const { return true; }
};
extern HardwareSerial Serial;
extern HardwareSerial Serial1;

class String {
public:
  String() {}
  String(const char *s) : _s(s ? s : "") {}
  String(char c) : _s(1, c) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned int v) : _s(std::to_string(v)) {}
  String(long v) : _s(std::to_string(v)) {}
  String(unsigned long v) : _s(std::to_string(v)) {}
  String(double v, int d = 2) {
    char b[32];
    snprintf(b, sizeof(b), "%.*f", d, v);
    _s = b;
  }
  String(float v, int d = 2) {
    char b[32];
    snprintf(b, sizeof(b), "%.*f", d, (double)v);
    _s = b;
  }
  const char *c_str() const { return _s.c_str(); }
  size_t length() const { return _s.size(); }
  size_t size() const { return _s.size(); }
  bool isEmpty() const { return _s.empty(); }
  char charAt(size_t i) const { return i < _s.size() ? _s[i] : 0; }
  char operator[](size_t i) const { return charAt(i); }
  char &operator[](size_t i) { return _s[i]; }
  String &operator+=(const String &o) {
    _s += o._s;
    return *this;
  }
  String &operator+=(const char *o) {
    if (o)
      _s += o;
    return *this;
  }
  String &operator+=(char c) {
    _s += c;
    return *this;
  }
  String operator+(const String &o) const {
    String r = *this;
    r += o;
    return r;
  }
  String operator+(const char *o) const {
    String r = *this;
    r += o;
    return r;
  }
  bool operator==(const String &o) const { return _s == o._s; }
  bool operator==(const char *o) const { return o && _s == o; }
  bool operator!=(const String &o) const { return !(*this == o); }
  bool operator<(const String &o) const { return _s < o._s; }
  bool equalsIgnoreCase(const char *o) const {
    return o && ::strcasecmp(_s.c_str(), o) == 0;
  }
  bool equalsIgnoreCase(const String &o) const {
    return ::strcasecmp(_s.c_str(), o._s.c_str()) == 0;
  }
  bool startsWith(const char *s) const { return _s.rfind(s, 0) == 0; }
  bool startsWith(const String &s) const { return startsWith(s.c_str()); }
  bool endsWith(const char *s) const {
    size_t l = strlen(s);
    return _s.size() >= l && _s.compare(_s.size() - l, l, s) == 0;
  }
  bool endsWith(const String &s) const { return endsWith(s.c_str()); }
  int indexOf(char c, size_t f = 0) const {
    auto p = _s.find(c, f);
    return p == std::string::npos ? -1 : (int)p;
  }
  int indexOf(const char *s, size_t f = 0) const {
    auto p = _s.find(s, f);
    return p == std::string::npos ? -1 : (int)p;
  }
  String substring(size_t f, size_t t = std::string::npos) const {
    return String(_s.substr(f, t == std::string::npos ? t : t - f).c_str());
  }
  String toLowerCase() const {
    String r;
    r._s = _s;
    for (auto &c : r._s)
      c = ::tolower(c);
    return r;
  }
  String toUpperCase() const {
    String r;
    r._s = _s;
    for (auto &c : r._s)
      c = ::toupper(c);
    return r;
  }
  void trim() {
    auto s = _s.find_first_not_of(" \t\r\n");
    auto e = _s.find_last_not_of(" \t\r\n");
    _s = (s == std::string::npos ? "" : _s.substr(s, e - s + 1));
  }
  long toInt() const { return strtol(_s.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(_s.c_str(), nullptr); }
  double toDouble() const { return strtod(_s.c_str(), nullptr); }
  void reserve(size_t n) { _s.reserve(n); }
  void clear() { _s.clear(); }
  operator const char *() const { return _s.c_str(); }
  std::string _s;
};
inline String operator+(const char *l, const String &r) {
  String s(l);
  s += r;
  return s;
}
inline bool operator==(const char *l, const String &r) { return r == l; }

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#ifndef F_CPU
#define F_CPU 160000000UL
#endif

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  size_t write(const char *s) {
    size_t n = 0;
    while (*s)
      n += write((uint8_t)*s++);
    return n;
  }
  size_t write(const uint8_t *b, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++)
      w += write(b[i]);
    return w;
  }
  template <typename T> size_t print(T) { return 0; }
  template <typename T> size_t println(T) { return 0; }
  size_t print(const char *s) { return write(s); }
  size_t println() { return 2; }
  size_t println(const char *s) {
    size_t n = write(s);
    return n + 2;
  }
};

class Stream : public Print {
public:
  virtual ~Stream() {}
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}
  void setTimeout(unsigned long) {}
};

#ifndef ARDUINO_ARCH_ESP32
#define ARDUINO_ARCH_ESP32
#endif
#ifndef ESP32
#define ESP32
#endif
#ifndef ARDUINO
#define ARDUINO 10815
#endif
