// RAM-beli access-token tar az uj szolgaltatokhoz (Gemini/ChatGPT/Grok). NVS-be nem kerul (meret, tervdoksi 2.13/b):
// ujrainditas utan a refresh tokenbol potlodik. Titok: soha nem logolva, a feluletre nem kerul.
#pragma once
#include <Arduino.h>
#include <time.h>

#include "config.h"

class TokenCache {
 public:
  void begin();
  // true + out, ha van token es legalabb marginS masodpercig meg ervenyes.
  bool get(int idx, String &out, time_t now, uint32_t marginS);
  void set(int idx, const String &access, time_t expiresAt);
  void clear(int idx);
  bool has(int idx);  // van-e (lejarattol fuggetlenul) — csak allapotkijelzeshez

 private:
  char _tok[MAX_CLAUDE_PROFILES][PROVIDER_ACCESS_MAX + 1] = {};
  time_t _exp[MAX_CLAUDE_PROFILES] = {};
  SemaphoreHandle_t _mtx = nullptr;
};

extern TokenCache tokenCache;
