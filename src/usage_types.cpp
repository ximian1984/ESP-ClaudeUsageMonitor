#include "usage_types.h"

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
