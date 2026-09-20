#include "https_util.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "ca_certs.h"
#include "config.h"

namespace {
// A body-t legfeljebb limit bajtig gyujti; a tobbit elnyeli, hogy a kapcsolat rendben lezarodjon.
class LimitedSink : public Stream {
 public:
  LimitedSink(String &dst, size_t limit) : _dst(dst), _limit(limit) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t size) override {
    size_t room = _dst.length() < _limit ? _limit - _dst.length() : 0;
    size_t n = size < room ? size : room;
    if (n) _dst.concat((const char *)buf, n);
    if (n < size) overflow = true;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool overflow = false;

 private:
  String &_dst;
  size_t _limit;
};
}  // namespace

HttpsResult httpsRequest(const char *logTag, const char *method, const String &url, const HttpHeaders &headers,
                         const char *contentType, const String &body, size_t maxBody) {
  HttpsResult r;
  WiFiClientSecure tls;
  tls.setCACert(CLAUDE_ROOT_CAS);  // tanusitvany-ellenorzes; setInsecure() nincs
  tls.setHandshakeTimeout(CLAUDE_HTTP_TIMEOUT_MS / 1000);
  HTTPClient http;
  http.setConnectTimeout(CLAUDE_HTTP_TIMEOUT_MS);
  http.setTimeout(CLAUDE_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  // HTTP/1.1 (alapertelmezett), UA "ESP32HTTPClient": ezzel mert kihivas-mentes valasz minden hasznalt hoszton (tervdoksi 2.13/b).
  if (!http.begin(tls, url)) {
    r.status = HTTPC_ERROR_CONNECTION_REFUSED;
    Serial.printf("[%s] %s: begin hiba\n", logTag, method);
    return r;
  }
  static const char *kCollect[] = {"cf-mitigated", "retry-after"};
  http.collectHeaders(kCollect, 2);
  for (const auto &h : headers) http.addHeader(h.first, h.second);  // titok: logba NEM kerul
  if (contentType) http.addHeader("Content-Type", contentType);

  int code = strcmp(method, "POST") == 0 ? http.POST((uint8_t *)body.c_str(), body.length()) : http.GET();
  r.status = code;
  if (code > 0) {
    r.cfMitigated = http.header("cf-mitigated");
    r.retryAfter = http.header("retry-after");
    LimitedSink sink(r.body, maxBody);
    int n = http.writeToStream(&sink);
    if (n < 0 && r.body.isEmpty()) r.status = n;
    r.overflow = sink.overflow;
  }
  http.end();
  // Csak host+statusz+meret (a URL-ben nincs titok; a query-t nem irjuk ki).
  int hostStart = url.indexOf("://");
  int pathStart = url.indexOf('/', hostStart + 3);
  String host = url.substring(hostStart + 3, pathStart < 0 ? url.length() : pathStart);
  Serial.printf("[%s] %s %s: HTTP %d, %u B%s\n", logTag, method, host.c_str(), r.status, (unsigned)r.body.length(),
                r.overflow ? " (levagva)" : "");
  return r;
}

String formEncode(const String &s) {
  static const char *hex = "0123456789ABCDEF";
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += c;
    } else {
      o += '%';
      o += hex[(c >> 4) & 0xF];
      o += hex[c & 0xF];
    }
  }
  return o;
}
