#pragma once
// ESP-IDF debug logging helpers - replaces Arduino Serial.print/println

#include "esp_log.h"
#include <stdarg.h>
#include <string>

#ifdef DEBUG_SERIAL

#define DEBUG_TAG "app"

inline void debugPrintln() { ESP_LOGI(DEBUG_TAG, ""); }
inline void debugPrintln(const char *msg) { ESP_LOGI(DEBUG_TAG, "%s", msg); }
inline void debugPrintln(const std::string &msg) {
  ESP_LOGI(DEBUG_TAG, "%s", msg.c_str());
}
inline void debugPrintln(int val) { ESP_LOGI(DEBUG_TAG, "%d", val); }
inline void debugPrintln(unsigned int val) { ESP_LOGI(DEBUG_TAG, "%u", val); }
inline void debugPrintln(unsigned long val) { ESP_LOGI(DEBUG_TAG, "%lu", val); }

template <typename... Args>
inline void debugPrintln(const char *fmt, Args... args) {
  char buf[256];
  snprintf(buf, sizeof(buf), fmt, args...);
  ESP_LOGI(DEBUG_TAG, "%s", buf);
}

inline void debugPrint(const char *msg) { ESP_LOGI(DEBUG_TAG, "%s", msg); }
inline void debugPrint(const std::string &msg) {
  ESP_LOGI(DEBUG_TAG, "%s", msg.c_str());
}
inline void debugPrint(int val) { ESP_LOGI(DEBUG_TAG, "%d", val); }
inline void debugPrint(unsigned int val) { ESP_LOGI(DEBUG_TAG, "%u", val); }
inline void debugPrint(unsigned long val) { ESP_LOGI(DEBUG_TAG, "%lu", val); }
inline void debugPrint(unsigned long val, int base) {
  if (base == 16)
    ESP_LOGI(DEBUG_TAG, "0x%lX", val);
  else
    ESP_LOGI(DEBUG_TAG, "%lu", val);
}
inline void debugPrint(float val, int digits = 3) {
  ESP_LOGI(DEBUG_TAG, "%.*f", digits, (double)val);
}

template <typename... Args>
inline void debugPrint(const char *fmt, Args... args) {
  char buf[256];
  snprintf(buf, sizeof(buf), fmt, args...);
  ESP_LOGI(DEBUG_TAG, "%s", buf);
}

#else

inline void debugPrintln() {}
inline void debugPrintln(const char *) {}
inline void debugPrintln(const std::string &) {}
inline void debugPrintln(int) {}
inline void debugPrintln(unsigned int) {}
inline void debugPrintln(unsigned long) {}
template <typename... Args> inline void debugPrintln(const char *, Args...) {}
inline void debugPrint(const char *) {}
inline void debugPrint(const std::string &) {}
inline void debugPrint(int) {}
inline void debugPrint(unsigned int) {}
inline void debugPrint(unsigned long) {}
inline void debugPrint(unsigned long, int) {}
inline void debugPrint(float, int = 3) {}
template <typename... Args> inline void debugPrint(const char *, Args...) {}

#endif
