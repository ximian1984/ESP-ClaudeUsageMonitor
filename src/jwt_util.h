// JWT-payload kiolvasasa (alairas-ellenorzes NELKUL: a token TLS-en at kozvetlenul a token-vegponttol jon).
// Host-tesztelheto (test/host/test_providers.cpp).
#pragma once
#include <Arduino.h>

// A JWT kozepso reszenek base64url-dekodolt JSON-ja; ures, ha nem JWT-alak.
String jwtPayloadJson(const String &jwt);

// ChatGPT: id_token -> "https://api.openai.com/auth".chatgpt_account_id (openai/codex login/src/token_data.rs:77-116).
String chatgptAccountIdFromIdToken(const String &idToken);
