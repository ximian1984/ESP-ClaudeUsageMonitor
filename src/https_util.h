// Kozos HTTPS-keres (CA-ellenorzessel, idokorlattal, valaszmeret-korlattal) a szolgaltatok login/refresh/fetch hivasaihoz.
// Titkot nem logol: csak metodus, host, statusz es meret kerul a soros naplora.
#pragma once
#include <Arduino.h>

#include <utility>
#include <vector>

struct HttpsResult {
  int status = 0;        // <0: HTTPClient-hibakod
  String body;           // legfeljebb maxBody bajt
  bool overflow = false; // a valasz nagyobb volt maxBody-nal
  String retryAfter;     // "Retry-After" fejlec (ha volt)
  String cfMitigated;    // "cf-mitigated" fejlec (Cloudflare-kihivas jele)
};

using HttpHeaders = std::vector<std::pair<const char *, String>>;

// method: "GET" vagy "POST". body csak POST-nal. A logTag a naplosor elotagja (pl. "gemini").
HttpsResult httpsRequest(const char *logTag, const char *method, const String &url, const HttpHeaders &headers,
                         const char *contentType, const String &body, size_t maxBody);

// application/x-www-form-urlencoded ertek-kodolas (RFC 3986 unreserved marad).
String formEncode(const String &s);
