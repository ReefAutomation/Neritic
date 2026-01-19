#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <stdarg.h>

#ifdef DEBUG_SERIAL
inline void debugPrintln() { Serial.println(); }
inline void debugPrintln(const char* msg) { Serial.println(msg); }
inline void debugPrintln(const String& msg) { Serial.println(msg); }
inline void debugPrintln(int val) { Serial.println(val); }
inline void debugPrintln(unsigned int val) { Serial.println(val); }
inline void debugPrintln(unsigned long val) { Serial.println(val); }
inline void debugPrintln(const IPAddress& ip) { Serial.println(ip); }
inline void debugPrintln(float val, int digits = 3) { Serial.println(val, digits); }
inline void debugPrintln(uint32_t val, int base) { Serial.println(val, base); }

inline void debugPrintIp(uint32_t ip) {
	IPAddress ipa(ip);
	Serial.println(ipa);
}

inline void debugPrint(const char* msg) { Serial.print(msg); }
inline void debugPrint(const String& msg) { Serial.print(msg); }
inline void debugPrint(int val) { Serial.print(val); }
inline void debugPrint(unsigned int val) { Serial.print(val); }
inline void debugPrint(unsigned long val) { Serial.print(val); }
inline void debugPrint(unsigned long val, int base) { Serial.print(val, base); }
inline void debugPrint(float val, int digits = 3) { Serial.print(val, digits); }
inline void debugPrint(uint32_t val, int base) { Serial.print(val, base); }

// Only use this printf-style overload if no other overload matches
template<typename... Args>
inline void debugPrint(const char* fmt, Args... args) {
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, args...);
    Serial.print(buf);
}

#else
inline void debugPrintln() {}
inline void debugPrintln(const char*) {}
inline void debugPrintln(const String&) {}
inline void debugPrintln(int) {}
inline void debugPrintln(unsigned int) {}
inline void debugPrintln(unsigned long) {}
inline void debugPrintln(const IPAddress&) {}

inline void debugPrintIp(uint32_t) {}

inline void debugPrint(const char*) {}
inline void debugPrint(const String&) {}
inline void debugPrint(int) {}
inline void debugPrint(unsigned int) {}
inline void debugPrint(unsigned long) {}
#endif
