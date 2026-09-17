// JSON-valasz -> UsageData.
//
// A valos valasz (2026-09-16, api.anthropic.com/api/oauth/usage, PLAN.md 2.6) ket helyen adja ugyanazt:
//   1) "limits": [{kind, group, percent, severity, resets_at, scope, is_active}, ...]  -> ELSODLEGES
//   2) "five_hour" / "seven_day": {utilization, resets_at, ...}                          -> TARTALEK
// Ismeretlen kulcsok, null codename-mezok nem zavarnak. Hibas JSON-ra nem omlik ossze.
//
// Kapu: alapbol NYITVA (az alak valos mintaval igazolt). -DUSAGE_PARSER_ENABLE=0 visszazarja
// (felismert alak -> ParserPending), pl. ha a Claude API valtozik es gyanus az adat.
#pragma once
#include <Arduino.h>

#include "config.h"
#include "usage_types.h"

#ifndef USAGE_PARSER_ENABLE
#define USAGE_PARSER_ENABLE 1
#endif

enum class UsageSource : uint8_t {
  None,        // nem felismert
  LimitsArray, // limits[] adta a session- es/vagy heti limitet
  TopLevel,    // csak a five_hour / seven_day tartalek
  GeminiBuckets,   // Gemini retrieveUserQuota buckets[]
  CodexRateLimit,  // ChatGPT wham/usage rate_limit.primary/secondary_window
  GrokCredits,     // Grok billing config.creditUsagePercent / onDemand
};

// A kapu NELKULI ertelmezes (host-tesztelheto).
// Visszaad: None, ha legalabb a session- vagy a heti limit megvan; kulonben Parse.
FetchError parseUsageUngated(const char *body, size_t len, UsageData &out, UsageSource &source);

// Uj szolgaltatok (kapu NELKUL, host-tesztelheto). A valaszalak FORRASBOL van (PLAN 2.13/b), valos mintaval
// MEG NEM igazolt ⚠ [vason merendo]. A kijelzo ket helyere (Session/Weekly "slot") a ket legfontosabb limit kerul,
// sajat cimkevel; a tobbi LimitKind::Other.
//   Gemini : buckets[]{modelId, tokenType, remainingFraction, resetTime} -> hasznalt % = (1 - remainingFraction) * 100
//   ChatGPT: rate_limit.primary_window / secondary_window {used_percent, limit_window_seconds, reset_at (epoch)}
//   Grok   : config.creditUsagePercent vagy onDemandUsed.val / onDemandCap.val; reset: currentPeriod.end / billingPeriodEnd
FetchError parseProviderUngated(ClaudeTransport transport, const char *body, size_t len, UsageData &out, UsageSource &source);

// A firmware ezt hivja: a transport szerinti parser + (Claude-nal) a kapu.
FetchError parseUsage(ClaudeTransport transport, const String &body, UsageData &out);
