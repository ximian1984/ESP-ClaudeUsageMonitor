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

bool isValidOrgId(const char *s) {  // UUID-alak (a claude.ai path-validacioja szerint, PLAN.md 2.2)
  if (!s || strlen(s) != 36) return false;
  for (int i = 0; i < 36; i++) {
    char c = s[i];
    bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
    if (dash ? c != '-' : !isxdigit((unsigned char)c)) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// TRANSPORTOK — minden host/path/fejlec itt, egy helyen. A projektgazda donteseig mindketto elerheto.
// ---------------------------------------------------------------------------------------------

// A) WebSession: claude.ai web.
//    Mert (hamis ertekekkel, PLAN.md 2.1-2.3): HTTP/1.1-en atjut a Cloudflare-en; a "sessionKey=sk-ant-sid01-..."
//    sutit kulon agon kezeli; OAuth-tokent a web vegpont elutasit ("oauth_token_not_accepted", a koordinator merese).
//    Fejlecek forrasa: github.com/linuxlewis/claude-usage @ ac15351, UsageService.swift:30-31.
//    ⚠ [feltarando] 200-as valasz ezen az uton meg nincs merve; az alak azonossaga az OAuth-valasszal feltetelezes.
static void authWebSession(HTTPClient &http, const char *auth) {
  http.addHeader("anthropic-client-platform", "web_claude_ai");
  String cookie = String("sessionKey=") + auth;
  http.addHeader("Cookie", cookie);  // logba NEM kerul
}

// B) OAuth: api.anthropic.com.
//    Mert: 200 + valos minta Bearer <oat01-token> + "anthropic-beta: oauth-2025-04-20" fejleccel, HTTP/1.1,
//    Cloudflare-kihivas nelkul (a koordinator, 2026-09-16). Hamis tokennel (sajat meres, 2026-09-16): 401
//    authentication_error "OAuth access token is invalid."; hitelesites nelkul 429 rate_limit_error + Retry-After.
//    A token lejarat elotti frissitese NEM itt van: refresh_scheduler.cpp ensureOAuthToken() + oauth_client (PLAN 2.9).
static void authOAuth(HTTPClient &http, const char *auth) {
  http.addHeader("anthropic-beta", "oauth-2025-04-20");
  String bearer = String("Bearer ") + auth;
  http.addHeader("Authorization", bearer);  // logba NEM kerul
}

struct TransportSpec {
  const char *name;
  const char *host;
  const char *pathPrefix;  // WebSession: + orgId + pathSuffix; OAuth: teljes path
  const char *pathSuffix;
  bool needsOrgId;
  void (*applyAuth)(HTTPClient &, const char *);
};

static const TransportSpec kTransports[CLAUDE_TRANSPORT_COUNT] = {
    {"web-session", "claude.ai", "/api/organizations/", "/usage", true, authWebSession},
    {"oauth", "api.anthropic.com", "/api/oauth/usage", "", false, authOAuth},
};

static const TransportSpec *spec(ClaudeTransport t) {
  uint8_t i = (uint8_t)t;
  return i < CLAUDE_TRANSPORT_COUNT ? &kTransports[i] : nullptr;
}

const char *transportName(ClaudeTransport t) { return spec(t) ? spec(t)->name : "?"; }
bool transportNeedsOrgId(ClaudeTransport t) { return spec(t) && spec(t)->needsOrgId; }

ClaudeResponse fetchUsage(ClaudeTransport transport, const char *orgId, const char *auth) {
  ClaudeResponse r;
  const TransportSpec *ts = spec(transport);
  if (!ts || !auth || !auth[0] || (ts->needsOrgId && (!orgId || !isValidOrgId(orgId)))) {
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
  // HTTP/1.1 marad (alapertelmezett). Mert (claude.ai, sessionKey-ut): HTTP/1.1-en a Cloudflare atengedett, HTTP/2-n
  // kihivast adott. Az OAuth-ut (api.anthropic.com) HTTP/1.1-en kihivas nelkul valaszol.
  // A User-Agent az alapertelmezett "ESP32HTTPClient" — curl-lal ezzel a UA-val mert atjutas (PLAN.md 2.1).

  String url = String("https://") + ts->host + ts->pathPrefix;
  if (ts->needsOrgId) url += String(orgId) + ts->pathSuffix;
  if (!http.begin(tls, url)) {
    r.error = FetchError::Connect;
    return r;
  }
  static const char *kCollect[] = {"cf-mitigated", "content-type", "retry-after"};
  http.collectHeaders(kCollect, 3);
  ts->applyAuth(http, auth);

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
    long ra = http.header("retry-after").toInt();  // masodperc (mert: "Retry-After: 1106"); HTTP-datum alakot nem kezel
    if (ra > 0) r.retryAfterS = (uint32_t)ra;
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
  Serial.printf("[claude] %s: HTTP %d, body %u B, hiba=%s\n", ts->name, code, (unsigned)r.body.length(), fetchErrorTitle(r.error));
  return r;
}
