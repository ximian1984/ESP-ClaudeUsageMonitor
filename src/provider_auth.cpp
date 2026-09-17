#include "provider_auth.h"

#include <ArduinoJson.h>

#include "https_util.h"
#include "jwt_util.h"
#include "oauth_client.h"

static const size_t TOKEN_BODY_MAX = 12288;  // OpenAI: id_token + access_token JWT-k (⚠ [vason merendo] pontos meret)

bool providerIsOAuth(ClaudeTransport t) { return t != ClaudeTransport::WebSession; }

bool providerUsesRamAccess(ClaudeTransport t) {
  return t == ClaudeTransport::Gemini || t == ClaudeTransport::ChatGpt || t == ClaudeTransport::Grok;
}

const char *providerLabel(ClaudeTransport t) {
  switch (t) {
    case ClaudeTransport::OAuth:
    case ClaudeTransport::WebSession:
      return "Claude";
    case ClaudeTransport::Gemini:
      return "Gemini";
    case ClaudeTransport::ChatGpt:
      return "ChatGPT";
    case ClaudeTransport::Grok:
      return "Grok";
  }
  return "?";
}

// ---------------------------------------------------------------------------------------------
// Token-valasz feldolgozasa (kozos: access_token, refresh_token, expires_in, scope, id_token; hiba: error/error.code)
// ---------------------------------------------------------------------------------------------
static ProviderTokens parseTokenResponse(const HttpsResult &h, ClaudeTransport t) {
  ProviderTokens r;
  JsonDocument doc;
  bool json = h.body.length() && !deserializeJson(doc, h.body);
  if (h.status == 200 && json && doc["access_token"].is<const char *>()) {
    r.ok = true;
    r.access = doc["access_token"].as<const char *>();
    if (doc["refresh_token"].is<const char *>()) r.refresh = doc["refresh_token"].as<const char *>();
    if (doc["expires_in"].is<uint32_t>()) r.expiresIn = doc["expires_in"].as<uint32_t>();
    if (doc["scope"].is<const char *>()) r.scope = doc["scope"].as<const char *>();
    if (t == ClaudeTransport::ChatGpt && doc["id_token"].is<const char *>())
      r.accountId = chatgptAccountIdFromIdToken(doc["id_token"].as<const char *>());
    Serial.printf("[auth] %s token OK: access %u B, refresh %s, expires_in=%u\n", providerLabel(t),
                  (unsigned)r.access.length(), r.refresh.length() ? "uj" : "nincs", (unsigned)r.expiresIn);
    return r;
  }
  // Hibak: RFC 6749 {"error":"invalid_grant"} (Google, xAI) vagy OpenAI {"error":{"code":"token_expired",...}}
  String code;
  if (json) {
    if (doc["error"].is<const char *>()) code = doc["error"].as<const char *>();
    else if (doc["error"]["code"].is<const char *>()) code = doc["error"]["code"].as<const char *>();
  }
  if (code == "authorization_pending") {
    r.pending = true;
    return r;
  }
  if (code == "slow_down") {
    r.pending = true;
    r.slowDown = true;
    return r;
  }
  r.error = String("HTTP ") + h.status + (code.length() ? " " + code : String());
  // Vegleges: invalid_grant (mert: Google/xAI hamis refresh -> 400 invalid_grant); OpenAI: 401 token_expired (mert) es a
  // codex vegleges kodjai (manager.rs:1660-1670).
  r.invalidGrant = code == "invalid_grant" || code == "access_denied" || code == "expired_token" ||
                   code == "refresh_token_expired" || code == "refresh_token_reused" ||
                   code == "refresh_token_invalidated" || (t == ClaudeTransport::ChatGpt && h.status == 401);
  Serial.printf("[auth] %s token hiba: %s%s\n", providerLabel(t), r.error.c_str(), r.invalidGrant ? " (vegleges)" : "");
  return r;
}

static ProviderTokens fromClaude(const OAuthTokens &o) {
  ProviderTokens r;
  r.ok = o.ok;
  r.access = o.access;
  r.refresh = o.refresh;
  r.expiresIn = o.expiresIn;
  r.scope = o.scope;
  r.error = o.error;
  r.invalidGrant = o.invalidGrant;
  return r;
}

// ---------------------------------------------------------------------------------------------
// Login inditasa
// ---------------------------------------------------------------------------------------------
bool providerBeginLogin(ClaudeTransport t, PendingLogin &out, String &err) {
  out.clear();
  out.transport = t;
  switch (t) {
    case ClaudeTransport::OAuth: {
      OAuthLogin lg;
      if (!oauthBeginLogin(lg)) {
        err = "PKCE init failed";
        return false;
      }
      out.mode = LoginMode::CodePaste;
      out.url = lg.authorizeUrl;
      out.verifier = lg.verifier;
      out.state = lg.state;
      return true;
    }
    case ClaudeTransport::Gemini: {
      out.mode = LoginMode::CodePaste;
      out.verifier = randomBase64url(32);
      out.state = randomBase64url(24);
      // gemini-cli authWithUserCode (oauth2.ts:441-452) + prompt=consent, hogy mindig jojjon refresh token
      // (a Google csak az elso hozzajarulasnal ad, ha nincs prompt=consent).
      out.url = String(GEMINI_AUTHORIZE_URL) + "?client_id=" + urlEncode(GEMINI_CLIENT_ID) +
                "&redirect_uri=" + urlEncode(GEMINI_REDIRECT_URI) + "&response_type=code&access_type=offline&prompt=consent" +
                "&scope=" + urlEncode(GEMINI_SCOPES) + "&code_challenge=" + pkceChallenge(out.verifier) +
                "&code_challenge_method=S256&state=" + urlEncode(out.state);
      return true;
    }
    case ClaudeTransport::ChatGpt: {
      // codex device_code_auth.rs: POST {issuer}/api/accounts/deviceauth/usercode {client_id}
      // mert valasz: device_auth_id (43), user_code (10), interval (STRING), expires_at
      JsonDocument req;
      req["client_id"] = CHATGPT_CLIENT_ID;
      String body;
      serializeJson(req, body);
      HttpsResult h = httpsRequest("chatgpt", "POST", String(CHATGPT_AUTH_BASE) + "/api/accounts/deviceauth/usercode", {},
                                   "application/json", body, 4096);
      JsonDocument doc;
      if (h.status != 200 || deserializeJson(doc, h.body) || !doc["device_auth_id"].is<const char *>()) {
        err = String("device code request failed (HTTP ") + h.status + ")";
        return false;
      }
      out.mode = LoginMode::Device;
      out.deviceId = doc["device_auth_id"].as<const char *>();
      // device_code_auth.rs: #[serde(alias = "user_code", alias = "usercode")]
      out.userCode = doc["user_code"].is<const char *>() ? doc["user_code"].as<const char *>() : (doc["usercode"] | "");
      int iv = doc["interval"].is<const char *>() ? atoi(doc["interval"].as<const char *>()) : (doc["interval"] | 5);
      out.intervalS = iv > 0 ? (uint32_t)iv : 5;
      out.expiresS = 15 * 60;  // "expires in 15 minutes" (device_code_auth.rs prompt)
      out.url = String(CHATGPT_AUTH_BASE) + "/codex/device";
      return out.userCode.length() > 0;
    }
    case ClaudeTransport::Grok: {
      // grok-build device_code.rs:110-150: form {client_id, scope, referrer}
      String body = String("client_id=") + formEncode(GROK_CLIENT_ID) + "&scope=" + formEncode(GROK_SCOPES) +
                    "&referrer=grok-build";
      HttpsResult h = httpsRequest("grok", "POST", String(GROK_AUTH_BASE) + "/oauth2/device/code", {},
                                   "application/x-www-form-urlencoded", body, 4096);
      JsonDocument doc;
      if (h.status != 200 || deserializeJson(doc, h.body) || !doc["device_code"].is<const char *>()) {
        err = String("device code request failed (HTTP ") + h.status + ")";
        return false;
      }
      out.mode = LoginMode::Device;
      out.deviceId = doc["device_code"].as<const char *>();
      out.userCode = doc["user_code"] | "";
      out.intervalS = doc["interval"] | 5;
      out.expiresS = doc["expires_in"] | 1800;
      const char *complete = doc["verification_uri_complete"] | "";
      out.url = complete[0] ? String(complete) : String(doc["verification_uri"] | "https://accounts.x.ai/oauth2/device");
      return out.userCode.length() > 0;
    }
    default:
      err = "not an OAuth source";
      return false;
  }
}

// ---------------------------------------------------------------------------------------------
// Kod-bemasolas (Claude, Gemini)
// ---------------------------------------------------------------------------------------------
ProviderTokens providerFinishCode(const PendingLogin &lg, const String &raw) {
  if (lg.transport == ClaudeTransport::OAuth) return fromClaude(oauthExchangeCode(raw, lg.verifier, lg.state));
  ProviderTokens r;
  if (lg.transport != ClaudeTransport::Gemini) {
    r.error = "not a code-paste source";
    return r;
  }
  String code = raw;
  code.trim();
  int hash = code.indexOf('#');  // ha valaki code#state-et masol, a state-et ellenorizzuk
  if (hash >= 0) {
    if (code.substring(hash + 1) != lg.state) {
      r.error = "state mismatch";
      return r;
    }
    code = code.substring(0, hash);
  }
  if (code.isEmpty()) {
    r.error = "empty code";
    return r;
  }
  String body = String("grant_type=authorization_code&code=") + formEncode(code) + "&code_verifier=" +
                formEncode(lg.verifier) + "&redirect_uri=" + formEncode(GEMINI_REDIRECT_URI) +
                "&client_id=" + formEncode(GEMINI_CLIENT_ID) + "&client_secret=" + formEncode(GEMINI_CLIENT_SECRET);
  HttpsResult h = httpsRequest("gemini", "POST", GEMINI_TOKEN_URL, {}, "application/x-www-form-urlencoded", body, TOKEN_BODY_MAX);
  r = parseTokenResponse(h, lg.transport);
  if (r.ok && r.refresh.isEmpty()) {
    r.ok = false;
    r.error = "Google returned no refresh token (remove the app's access in your Google account, then retry)";
  }
  return r;
}

// ---------------------------------------------------------------------------------------------
// Eszkozkod (ChatGPT, Grok)
// ---------------------------------------------------------------------------------------------
ProviderTokens providerPollDevice(const PendingLogin &lg) {
  ProviderTokens r;
  if (lg.transport == ClaudeTransport::ChatGpt) {
    JsonDocument req;
    req["device_auth_id"] = lg.deviceId;
    req["user_code"] = lg.userCode;
    String body;
    serializeJson(req, body);
    HttpsResult h = httpsRequest("chatgpt", "POST", String(CHATGPT_AUTH_BASE) + "/api/accounts/deviceauth/token", {},
                                 "application/json", body, 8192);
    if (h.status == 403 || h.status == 404) {  // device_code_auth.rs: 403/404 = meg nincs jovahagyva
      r.pending = true;
      return r;
    }
    JsonDocument doc;
    if (h.status != 200 || deserializeJson(doc, h.body) || !doc["authorization_code"].is<const char *>() ||
        !doc["code_verifier"].is<const char *>()) {
      r.error = String("device auth failed (HTTP ") + h.status + ")";
      return r;
    }
    // Csere: server.rs:809-843, redirect: device_code_auth.rs ({issuer}/deviceauth/callback)
    String code = doc["authorization_code"].as<const char *>();
    String verifier = doc["code_verifier"].as<const char *>();
    doc.clear();
    String form = String("grant_type=authorization_code&code=") + formEncode(code) + "&redirect_uri=" +
                  formEncode(String(CHATGPT_AUTH_BASE) + "/deviceauth/callback") + "&client_id=" +
                  formEncode(CHATGPT_CLIENT_ID) + "&code_verifier=" + formEncode(verifier);
    HttpsResult t = httpsRequest("chatgpt", "POST", String(CHATGPT_AUTH_BASE) + "/oauth/token", {},
                                 "application/x-www-form-urlencoded", form, TOKEN_BODY_MAX);
    r = parseTokenResponse(t, lg.transport);
    if (r.ok && r.refresh.isEmpty()) {
      r.ok = false;
      r.error = "no refresh token in response";
    }
    return r;
  }
  if (lg.transport == ClaudeTransport::Grok) {
    String form = String("grant_type=") + formEncode("urn:ietf:params:oauth:grant-type:device_code") + "&device_code=" +
                  formEncode(lg.deviceId) + "&client_id=" + formEncode(GROK_CLIENT_ID);
    HttpsResult h = httpsRequest("grok", "POST", String(GROK_AUTH_BASE) + "/oauth2/token", {},
                                 "application/x-www-form-urlencoded", form, TOKEN_BODY_MAX);
    r = parseTokenResponse(h, lg.transport);
    if (r.ok && r.refresh.isEmpty()) {
      r.ok = false;
      r.error = "no refresh token in response (offline_access?)";
    }
    return r;
  }
  r.error = "not a device-code source";
  return r;
}

// ---------------------------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------------------------
ProviderTokens providerRefresh(ClaudeTransport t, const char *refreshToken, const char *scope) {
  if (t == ClaudeTransport::OAuth) return fromClaude(oauthRefresh(refreshToken, scope));
  ProviderTokens r;
  if (!refreshToken || !refreshToken[0]) {
    r.error = "no refresh token";
    r.invalidGrant = true;
    return r;
  }
  HttpsResult h;
  switch (t) {
    case ClaudeTransport::Gemini: {
      String body = String("grant_type=refresh_token&refresh_token=") + formEncode(refreshToken) +
                    "&client_id=" + formEncode(GEMINI_CLIENT_ID) + "&client_secret=" + formEncode(GEMINI_CLIENT_SECRET);
      h = httpsRequest("gemini", "POST", GEMINI_TOKEN_URL, {}, "application/x-www-form-urlencoded", body, TOKEN_BODY_MAX);
      break;
    }
    case ClaudeTransport::ChatGpt: {  // manager.rs:1605-1625: JSON {client_id, grant_type, refresh_token}
      JsonDocument req;
      req["client_id"] = CHATGPT_CLIENT_ID;
      req["grant_type"] = "refresh_token";
      req["refresh_token"] = refreshToken;
      String body;
      serializeJson(req, body);
      h = httpsRequest("chatgpt", "POST", String(CHATGPT_AUTH_BASE) + "/oauth/token", {}, "application/json", body,
                       TOKEN_BODY_MAX);
      break;
    }
    case ClaudeTransport::Grok: {
      String body = String("grant_type=refresh_token&refresh_token=") + formEncode(refreshToken) +
                    "&client_id=" + formEncode(GROK_CLIENT_ID);
      h = httpsRequest("grok", "POST", String(GROK_AUTH_BASE) + "/oauth2/token", {}, "application/x-www-form-urlencoded",
                       body, TOKEN_BODY_MAX);
      break;
    }
    default:
      r.error = "not an OAuth source";
      return r;
  }
  return parseTokenResponse(h, t);
}

// ---------------------------------------------------------------------------------------------
// Gemini project (setup.ts:168-232)
// ---------------------------------------------------------------------------------------------
String geminiLoadProject(const String &access, String &err) {
  String body = "{\"metadata\":{\"ideType\":\"IDE_UNSPECIFIED\",\"platform\":\"PLATFORM_UNSPECIFIED\",\"pluginType\":\"GEMINI\"}}";
  HttpsResult h = httpsRequest("gemini", "POST", String(GEMINI_API_BASE) + ":loadCodeAssist",
                               {{"Authorization", String("Bearer ") + access}}, "application/json", body, 16384);
  JsonDocument doc;
  if (h.status != 200 || deserializeJson(doc, h.body)) {
    err = String("loadCodeAssist HTTP ") + h.status;
    return String();
  }
  const char *project = doc["cloudaicompanionProject"] | "";
  if (!project[0]) {
    err = "no Gemini Code Assist project for this account (run the Gemini CLI once to finish onboarding)";
    return String();
  }
  Serial.printf("[gemini] project-ID megvan (%u kar.)\n", (unsigned)strlen(project));
  return String(project);
}
