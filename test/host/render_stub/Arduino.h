// Host-stub a CYD-kijelzo gepi rendereleshez (test/host/render_cyd.sh). Bovebb, mint a stub/Arduino.h: a
// display_cyd.cpp String-muveletei, allithato millis(), ESP heap-lekerdezes (itt 0).
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
using std::max;
using std::min;
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  int indexOf(char c, int from = 0) const { auto p = find(c, (size_t)from); return p == npos ? -1 : (int)p; }
  bool isEmpty() const { return empty(); }
  void remove(size_t i) { erase(i); }
  String substring(size_t a) const { return substr(a); }
};
inline String operator+(const String &a, const char *b) { return String(std::string(a) + b); }
inline String operator+(const char *a, const String &b) { return String(a + std::string(b)); }
inline String operator+(const String &a, const String &b) { return String(std::string(a) + std::string(b)); }
extern uint32_t g_hostMillis;
inline uint32_t millis() { return g_hostMillis; }
inline uint32_t micros() { return g_hostMillis * 1000; }
inline void configTzTime(const char *, const char *, const char *) {}
#define HIGH 1
#define LOW 0
#define OUTPUT 1
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? (T)lo : v > hi ? (T)hi : v; }
struct HostSerial {
  void printf(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
  void println(const char *s) { puts(s); }
};
inline HostSerial Serial;
struct HostEsp {
  uint32_t getFreeHeap() { return 0; }
  uint32_t getMinFreeHeap() { return 0; }
  uint32_t getMaxAllocHeap() { return 0; }
};
inline HostEsp ESP;
typedef void *SemaphoreHandle_t;
