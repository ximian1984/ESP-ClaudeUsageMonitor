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

#include "usage_types.h"

#ifndef USAGE_PARSER_ENABLE
#define USAGE_PARSER_ENABLE 1
#endif

enum class UsageSource : uint8_t {
  None,        // nem felismert
  LimitsArray, // limits[] adta a session- es/vagy heti limitet
  TopLevel,    // csak a five_hour / seven_day tartalek
};

// A kapu NELKULI ertelmezes (host-tesztelheto).
// Visszaad: None, ha legalabb a session- vagy a heti limit megvan; kulonben Parse.
FetchError parseUsageUngated(const char *body, size_t len, UsageData &out, UsageSource &source);

// A firmware ezt hivja: parseUsageUngated + kapu.
FetchError parseUsage(const String &body, UsageData &out);
