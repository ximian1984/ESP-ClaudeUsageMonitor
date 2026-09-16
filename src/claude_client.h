// HTTPS GET a Claude usage-endpointra, a profilban valasztott transporton. Semmit nem tud a JSON-mezokrol (az a usage_parser dolga).
// Ha a Claude a webes API-t vagy a hitelesitest megvaltoztatja, elsosorban ezt es a parsert kell modositani (spec 25.).
#pragma once
#include <Arduino.h>

#include "usage_types.h"

struct ClaudeResponse {
  FetchError error = FetchError::None;  // None = 200 es a body beolvasva
  int httpStatus = 0;                   // <0: HTTPClient hibakod
  String body;
  uint32_t retryAfterS = 0;             // 429 + Retry-After (mert: api.anthropic.com kuldi)
};

const char *transportName(ClaudeTransport t);    // "web-session" / "oauth"
bool transportNeedsOrgId(ClaudeTransport t);

bool isValidOrgId(const char *orgId);  // UUID-alak (a szerver path-validacioja szerint, PLAN.md 2.2)

// Blokkolo (TLS-kezfogas + olvasas, legfeljebb ~CLAUDE_HTTP_TIMEOUT_MS) — ezert a refresh-taskbol hivando.
ClaudeResponse fetchUsage(ClaudeTransport transport, const char *orgId, const char *auth);
