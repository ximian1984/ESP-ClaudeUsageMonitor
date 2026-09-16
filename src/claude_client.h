// HTTPS GET a claude.ai usage-endpointra. Semmit nem tud a JSON-mezokrol (az a usage_parser dolga).
// Ha a Claude a webes API-t vagy a hitelesitest megvaltoztatja, elsosorban ezt es a parsert kell modositani (spec 25.).
#pragma once
#include <Arduino.h>

#include "usage_types.h"

struct ClaudeResponse {
  FetchError error = FetchError::None;  // None = 200 es a body beolvasva
  int httpStatus = 0;                   // <0: HTTPClient hibakod
  String body;
};

bool isValidOrgId(const char *orgId);  // UUID-alak (a szerver path-validacioja szerint, PLAN.md 2.2)

// Blokkolo (TLS-kezfogas + olvasas, legfeljebb ~CLAUDE_HTTP_TIMEOUT_MS) — ezert a refresh-taskbol hivando.
ClaudeResponse fetchUsage(const char *orgId, const char *auth);
