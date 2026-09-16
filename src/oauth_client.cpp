#include "oauth_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "base64url.h"
#include "ca_certs.h"
#include "config.h"

// A platform.claude.com lanca ISRG X1/X2-re fut (PLAN 2.8), amit a CLAUDE_ROOT_CAS mar tartalmaz.

static String randomBase64url(size_t nbytes) {
  uint8_t buf[48];
  if (nbytes > sizeof(buf)) nbytes = sizeof(buf);
  esp_fill_random(buf, nbytes);
  char out[68];
  base64urlEncode(buf, nbytes, out, sizeof(out));
  return String(out);
}

static String pkceChallenge(const String &verifier) {
  uint8_t hash[32];
  mbedtls_sha256((const unsigned char *)verifier.c_str(), verifier.length(), hash, 0);  // 0 = SHA-256
  char out[48];
  base64urlEncode(hash, sizeof(hash), out, sizeof(out));
  return String(out);
}

static String urlEncode(const String &s) {
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

bool oauthBeginLogin(OAuthLogin &out) {
  out.verifier = randomBase64url(32);  // 32 bajt -> 43 karakter, az RFC 7636 tartomanyban (43..128)
  out.state = randomBase64url(24);
  if (out.verifier.length() < 43) return false;
  String challenge = pkceChallenge(out.verifier);
  out.authorizeUrl = String(OAUTH_AUTHORIZE_URL) + "?code=true&client_id=" + OAUTH_CLIENT_ID +
                     "&response_type=code&redirect_uri=" + urlEncode(OAUTH_REDIRECT_URI) +
                     "&scope=" + urlEncode(OAUTH_SCOPES) + "&code_challenge=" + challenge +
                     "&code_challenge_method=S256&state=" + urlEncode(out.state);
  return true;
}

// A token-vegpont hivasa. bodyJson mar kesz JSON (a hivo allitja ossze, hogy a titok ne masolodjon feleslegesen).
static OAuthTokens postToken(const String &bodyJson) {
  OAuthTokens r;
  WiFiClientSecure tls;
  tls.setCACert(CLAUDE_ROOT_CAS);  // tanusitvany-ellenorzes; setInsecure() nincs
  tls.setHandshakeTimeout(CLAUDE_HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setConnectTimeout(CLAUDE_HTTP_TIMEOUT_MS);
  http.setTimeout(CLAUDE_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  String url = String("https://" OAUTH_TOKEN_HOST) + OAUTH_TOKEN_PATH;
  if (!http.begin(tls, url)) {
    r.error = "TLS/connect";
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t *)bodyJson.c_str(), bodyJson.length());
  // A token-valasz kicsi (nehany szaz bajt), getString() elegendo; a body-t nem logoljuk egeszben.
  String body = (code > 0) ? http.getString() : String();
  http.end();

  if (code != 200) {
    // A hibatorzs error-mezoje beszedes (pl. "invalid_grant"), de nem titok. A body-t nem logoljuk egeszben.
    JsonDocument ej;
    if (!deserializeJson(ej, body) && ej["error"].is<const char *>()) {
      r.error = String((int)code) + " " + ej["error"].as<const char *>();
      r.invalidGrant = strcmp(ej["error"].as<const char *>(), "invalid_grant") == 0;
    } else {
      r.error = String("HTTP ") + code;
    }
    Serial.printf("[oauth] token HTTP %d (%s)\n", code, r.error.c_str());
    return r;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    r.error = "parse";
    return r;
  }
  if (!doc["access_token"].is<const char *>()) {
    r.error = "no access_token";
    return r;
  }
  r.ok = true;
  r.access = doc["access_token"].as<const char *>();
  if (doc["refresh_token"].is<const char *>()) r.refresh = doc["refresh_token"].as<const char *>();
  if (doc["expires_in"].is<uint32_t>()) r.expiresIn = doc["expires_in"].as<uint32_t>();
  if (doc["scope"].is<const char *>()) r.scope = doc["scope"].as<const char *>();
  Serial.printf("[oauth] token OK, expires_in=%u, rotalt refresh=%s\n", (unsigned)r.expiresIn,
                r.refresh.isEmpty() ? "nem" : "igen");
  return r;
}

OAuthTokens oauthExchangeCode(const String &raw, const String &verifier, const String &expectedState) {
  OAuthTokens r;
  String s = raw;
  s.trim();
  int hash = s.indexOf('#');
  if (hash < 0) {
    r.error = "no #state";
    return r;
  }
  String code = s.substring(0, hash);
  String state = s.substring(hash + 1);
  if (code.isEmpty() || state.isEmpty()) {
    r.error = "empty code/state";
    return r;
  }
  if (state != expectedState) {  // CSRF: a visszakapott state-nek egyeznie kell
    r.error = "state mismatch";
    return r;
  }
  JsonDocument body;
  body["grant_type"] = "authorization_code";
  body["code"] = code;
  body["redirect_uri"] = OAUTH_REDIRECT_URI;
  body["client_id"] = OAUTH_CLIENT_ID;
  body["code_verifier"] = verifier;
  body["state"] = state;
  String out;
  serializeJson(body, out);
  return postToken(out);
}

OAuthTokens oauthRefresh(const char *refreshToken, const char *scope) {
  OAuthTokens r;
  if (!refreshToken || !refreshToken[0]) {
    r.error = "no refresh token";
    r.invalidGrant = true;
    return r;
  }
  JsonDocument body;
  body["grant_type"] = "refresh_token";
  body["refresh_token"] = refreshToken;
  body["client_id"] = OAUTH_CLIENT_ID;
  body["scope"] = (scope && scope[0]) ? scope : OAUTH_SCOPES;
  String out;
  serializeJson(body, out);
  return postToken(out);
}
