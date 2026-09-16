#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
struct String : std::string { using std::string::string; String(const char*s):std::string(s){} String(const std::string&s):std::string(s){} };
inline uint32_t millis(){return 0;}
inline void configTzTime(const char*,const char*,const char*){}
