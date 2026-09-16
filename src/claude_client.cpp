#include "claude_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "ca_certs.h"
#include "config.h"

// A body-t legfeljebb CLAUDE_MAX_BODY_BYTES-ig gyujti; ami folotte van, azt eldobja es jelzi.
// (A valasz chunked — mert, PLAN.md 2.2 —, ezert a Content-Length nem hasznalhato elore.)
class LimitedStringSink : public Stream {
 public:
  LimitedStringSink(String &dst, size_t limit) : _dst(dst), _limit(limit) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t size) override {
    size_t room = _dst.length() < _limit ? _limit - _dst.length() : 0;
    size_t n = size < room ? size : room;
    if (n) _dst.concat((const char *)buf, n);
    if (n < size) overflow = true;
    return size;  // a tobbit is "elnyeljuk", hogy a kapcsolat rendben lezarodjon
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

bool isValidOrgId(const char *s) {
  if (!s || strlen(s) != 36) return false;
  for (int i = 0; i < 36; i++) {
    char c = s[i];
    bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
    if (dash ? c != '-' : !isxdigit((unsigned char)c)) return false;
  }
  return true;
}

// ⚠ [feltarando] HITELESITES — IDEIGLENES.
// Mert (hamis ertekkel, PLAN.md 2.3): a szerver a "sessionKey=sk-ant-sid01-..." sutit kulon agon kezeli.
// Hogy ez ONMAGABAN eleg-e, es kell-e mas suti/fejlec, csak valos session-nel derul ki.
// A mechanizmus egyetlen helyen van, hogy a valos minta utan itt lehessen javitani.
static void applyAuth(HTTPClient &http, const char *auth) {
  // Forras: github.com/linuxlewis/claude-usage @ ac15351, ClaudeUsage/Services/UsageService.swift:30-31 —
  // a webes kliens ezt a ket fejlecet kuldi. A platform-fejlec hitelesites nelkul nem valtoztat a valaszon
  // (mert 2026-09-16, hamis sessionKey-jel: ugyanaz a 403 account_session_invalid, Cloudflare-kihivas nelkul);
  // hogy ervenyes session mellett kell-e, az ⚠ [feltarando].
  http.addHeader("anthropic-client-platform", "web_claude_ai");
  String cookie = String("sessionKey=") + auth;
  http.addHeader("Cookie", cookie);
  // A cookie String itt kiesik a scope-bol; a HTTPClient masolatot tart. Logba NEM kerul.
}

ClaudeResponse fetchUsage(const char *orgId, const char *auth) {
  ClaudeResponse r;
  if (!orgId || !auth || !auth[0] || !isValidOrgId(orgId)) {
    r.error = FetchError::NotConfigured;
    return r;
  }

  WiFiClientSecure tls;
  tls.setCACert(CLAUDE_ROOT_CAS);  // tanusitvany-ellenorzes; setInsecure() nincs (spec 19.)
  tls.setHandshakeTimeout(CLAUDE_HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setConnectTimeout(CLAUDE_HTTP_TIMEOUT_MS);
  http.setTimeout(CLAUDE_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  // HTTP/1.1 marad (alapertelmezett). Mert: HTTP/1.1-en a Cloudflare atengedett, HTTP/2-n kihivast adott.
  // A User-Agent az alapertelmezett "ESP32HTTPClient" — curl-lal ezzel a UA-val mert atjutas (PLAN.md 2.1).

  String url = String("https://" CLAUDE_HOST "/api/organizations/") + orgId + "/usage";
  if (!http.begin(tls, url)) {
    r.error = FetchError::Connect;
    return r;
  }
  static const char *kCollect[] = {"cf-mitigated", "content-type"};
  http.collectHeaders(kCollect, 2);
  applyAuth(http, auth);

  int code = http.GET();
  r.httpStatus = code;

  if (code < 0) {
    r.error = (code == HTTPC_ERROR_READ_TIMEOUT) ? FetchError::Timeout : FetchError::Connect;
  } else if (code == 403 && http.header("cf-mitigated").equalsIgnoreCase("challenge")) {
    r.error = FetchError::CfChallenge;
  } else if (code == 401 || code == 403) {
    r.error = FetchError::Auth;
  } else if (code == 429) {
    r.error = FetchError::RateLimited;
  } else if (code != 200) {
    r.error = FetchError::Http;
  } else {
    LimitedStringSink sink(r.body, CLAUDE_MAX_BODY_BYTES);
    int n = http.writeToStream(&sink);
    if (n < 0) {
      r.error = (n == HTTPC_ERROR_READ_TIMEOUT) ? FetchError::Timeout : FetchError::Connect;
      r.body = "";
    } else if (sink.overflow) {
      r.error = FetchError::TooLarge;
      r.body = "";
    }
  }
  http.end();
  // Logban csak statusz es meret — sem a suti, sem a body (spec 19.).
  Serial.printf("[claude] HTTP %d, body %u B, hiba=%s\n", code, (unsigned)r.body.length(), fetchErrorTitle(r.error));
  return r;
}
