// Szolgaltatonkenti login / token-frissites (Claude, Gemini, ChatGPT, Grok). Forras es meres: tervdoksi 2.13/b.
//   Claude  : PKCE, kezi "code#state" (oauth_client.*)
//   Gemini  : PKCE, kezi "code" (codeassist.google.com/authcode), form-kodolt token-kerés client_secret-tel
//   ChatGPT : OpenAI-fele eszkozkod (deviceauth/usercode -> deviceauth/token -> oauth/token), JWT-bol account-ID
//   Grok    : RFC 8628 eszkozkod (auth.x.ai/oauth2/device/code -> /oauth2/token)
// Minden fuggveny BLOKKOLO (TLS). Titkot nem logol.
#pragma once
#include <Arduino.h>

#include "config.h"

enum class LoginMode : uint8_t { None, CodePaste, Device };

struct PendingLogin {
  bool active = false;
  int idx = -1;
  uint32_t editSeq = 0;
  uint32_t startedMs = 0;
  ClaudeTransport transport = ClaudeTransport::OAuth;
  LoginMode mode = LoginMode::None;
  String url;             // CodePaste: authorize URL; Device: a felhasznalo altal megnyitando oldal
  String verifier, state; // CodePaste (PKCE)
  String deviceId;        // Device: OpenAI device_auth_id / RFC 8628 device_code — TITOK
  String userCode;        // Device: a felhasznalonak mutatott kod (nem titok)
  uint32_t intervalS = 5;
  uint32_t expiresS = 900;
  uint32_t lastPollMs = 0;
  void clear() { *this = PendingLogin(); }
};

struct ProviderTokens {
  bool ok = false;
  bool pending = false;       // eszkozkod: a felhasznalo meg nem hagyta jova
  bool slowDown = false;      // RFC 8628 slow_down
  bool invalidGrant = false;  // vegleges: ujra be kell lepni
  String error;               // beszedes, titok nelkul
  String access, refresh, scope;
  uint32_t expiresIn = 0;
  String accountId;           // ChatGPT: id_token -> chatgpt_account_id
};

bool providerIsOAuth(ClaudeTransport t);        // Claude OAuth, Gemini, ChatGPT, Grok
bool providerUsesRamAccess(ClaudeTransport t);  // Gemini, ChatGPT, Grok (az access token csak RAM-ban)
const char *providerLabel(ClaudeTransport t);   // "Claude", "Gemini", ...

// Login inditasa: CodePaste-nel csak URL-epites; Device-nal HTTPS-keres a kodert. false + err hibanal.
bool providerBeginLogin(ClaudeTransport t, PendingLogin &out, String &err);
// CodePaste: a bemasolt kod (Claude: "code#state"; Gemini: "code" vagy "code#state") -> tokenek.
ProviderTokens providerFinishCode(const PendingLogin &login, const String &raw);
// Device: egy lekerdezes. pending/slowDown -> meg varni kell.
ProviderTokens providerPollDevice(const PendingLogin &login);
// Refresh token -> uj access (es esetleg rotalt refresh).
ProviderTokens providerRefresh(ClaudeTransport t, const char *refreshToken, const char *scope);

// Gemini: loadCodeAssist -> cloudaicompanionProject (ures + err, ha a fiok nincs onboardolva).
String geminiLoadProject(const String &access, String &err);
