#pragma once
#include <Arduino.h>

#ifdef DEBUG_SERIAL
inline void debugPrintln() { Serial.println(); }
inline void debugPrintln(const char* msg) { Serial.println(msg); }
inline void debugPrintln(const String& msg) { Serial.println(msg); }
inline void debugPrintln(String& msg) { Serial.println(msg); }
inline void debugPrintln(int val) { Serial.println(val); }
inline void debugPrintln(unsigned long val) { Serial.println(val); }
template<typename T>
inline void debugPrintln(const T& val) {
#ifdef DEBUG_SERIAL
	Serial.println(val);
#else
	(void)val;
#endif
}

template<typename T>
inline void debugPrint(const T& val) {
#ifdef DEBUG_SERIAL
	Serial.print(val);
#else
	(void)val;
#endif
}
inline void debugPrint(const char* msg) { Serial.print(msg); }
inline void debugPrint(const String& msg) { Serial.print(msg); }
inline void debugPrint(String& msg) { Serial.print(msg); }
inline void debugPrint(int val) { Serial.print(val); }
inline void debugPrint(unsigned long val) { Serial.print(val); }
#else
inline void debugPrintln() {}
inline void debugPrintln(const char*) {}
inline void debugPrintln(const String&) {}
inline void debugPrintln(String&) {}
inline void debugPrintln(int) {}
inline void debugPrintln(unsigned long) {}
template<typename T>
inline void debugPrintln(const T& val) {
#ifdef DEBUG_SERIAL
	Serial.println(val);
#else
	(void)val;
#endif
}

template<typename T>
inline void debugPrint(const T& val) {
#ifdef DEBUG_SERIAL
	Serial.print(val);
#else
	(void)val;
#endif
}
inline void debugPrint(const char*) {}
inline void debugPrint(const String&) {}
inline void debugPrint(String&) {}
inline void debugPrint(int) {}
inline void debugPrint(unsigned long) {}
#endif
