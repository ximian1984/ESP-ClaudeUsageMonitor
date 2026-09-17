#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
struct String : std::string { using std::string::string; String(const char*s):std::string(s){} String(const std::string&s):std::string(s){}
  int indexOf(char c, int from = 0) const { auto p = find(c, (size_t)from); return p == npos ? -1 : (int)p; } };
inline uint32_t millis(){return 0;}
inline void configTzTime(const char*,const char*,const char*){}
#include <cctype>
#include <cstdarg>
struct HostSerial { void printf(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); } };
inline HostSerial Serial;
