// On-device OAuth: PKCE-login + token-frissites. Vegpontok/CLIENT_ID/PKCE forrasa: Claude Code 2.1.273 (PLAN 2.8).
// A refresh-elv mintaja: Netatmo/NetatmoAccess.cs Refresh() (PLAN 2.9). A titkot sehol nem logolja.
#pragma once
#include <Arduino.h>

struct OAuthLogin {
  String verifier;   // PKCE code_verifier (43 karakter, base64url)
  String state;      // CSRF-state
  String authorizeUrl;
};

struct OAuthTokens {
  bool ok = false;
  String access;
  String refresh;    // ha a valasz nem ad ujat, a hivo a regit tartja meg (rotacio opcionalis)
  uint32_t expiresIn = 0;   // masodperc
  String scope;
  String error;      // hibaszoveg (naplohoz/kijelzohoz; titkot nem tartalmaz)
  bool invalidGrant = false;  // a refresh/login token vegleg ervenytelen -> ujra-belepes kell
};

// PKCE-login inditasa: verifier+state+challenge generalas, authorize URL osszeallitasa.
// A challenge = base64url(SHA-256(verifier)). Vason ellenorizve az RFC 7636 B. vektorral (PLAN 2.9).
bool oauthBeginLogin(OAuthLogin &out);

// A callback-oldalrol beirt "code#state" feldolgozasa: state-egyezes ellenorzese, majd kodcsere tokenre.
// A raw a teljes beirt szoveg (tartalmazhat '#'-et). expectedState a login-bol.
OAuthTokens oauthExchangeCode(const String &raw, const String &verifier, const String &expectedState);

// Refresh token -> uj access (es esetleg rotalt refresh) token.
OAuthTokens oauthRefresh(const char *refreshToken, const char *scope);
