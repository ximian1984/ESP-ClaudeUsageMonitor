#include "usage_types.h"

uint16_t usageColor565(float leftPct) {
  float left = leftPct < 0 ? 0 : (leftPct > 100 ? 100 : leftPct);
  // Also fel: zold -> sarga (a piros no), felso fel: sarga -> zold (a zold no). Igy a kozep tiszta sarga.
  int r = left >= 50 ? (int)(255.0f * (100.0f - left) / 50.0f + 0.5f) : 255;
  int g = left >= 50 ? 255 : (int)(255.0f * left / 50.0f + 0.5f);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3));  // RGB565, kek = 0
}

const char *fetchErrorTitle(FetchError e) {
  switch (e) {
    case FetchError::None: return "";
    case FetchError::NoWifi: return "NO WIFI";
    case FetchError::NoTime: return "NTP";
    case FetchError::Connect: return "NO INTERNET";
    case FetchError::Timeout: return "TIMEOUT";
    case FetchError::CfChallenge: return "CLOUDFLARE";
    case FetchError::Auth: return "CLAUDE AUTH";
    case FetchError::RateLimited: return "RATE LIMIT";
    case FetchError::Http: return "CLAUDE HTTP";
    case FetchError::TooLarge: return "TOO LARGE";
    case FetchError::Parse: return "USAGE PARSE";
    case FetchError::ParserPending: return "PARSER TODO";
    case FetchError::NotConfigured: return "NOT SET UP";
    case FetchError::ReloginRequired: return "RE-LOGIN";
    case FetchError::RefreshFailed: return "TOKEN REFRESH";
  }
  return "ERROR";
}
