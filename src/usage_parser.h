// JSON-valasz -> UsageData.
//
// ⛔ KAPUZOTT: a valos, hitelesitett /usage valasz szerkezete meg NINCS merve (PLAN.md 2.4).
// A parser ket, KOZOSSEGI forrasbol ismert alakot tolerál (PLAN.md 2.5), de amig a projektgazda
// devtools-mintaja nem igazolja, melyiket adja ma a claude.ai, alapbol NEM ad ki adatot:
//   - felismert alak      -> ParserPending ("PARSER TODO" a kijelzon)
//   - nem felismert alak  -> Parse ("USAGE PARSE")
// Vason-probahoz a kapu build-flaggel nyithato: -DUSAGE_PARSER_ENABLE=1
#pragma once
#include <Arduino.h>

#include "usage_types.h"

enum class UsageShape : uint8_t {
  None,       // nem felismert
  TopLevel,   // A: {"five_hour":{"utilization":..,"resets_at":..}, "seven_day":{..}, "seven_day_*":..}
  RawLimits,  // B: {"raw_limits":{"five_hour":{"remaining_tokens":..,"total_tokens":..,"reset_at":..}, ..}}
};

// A kapu NELKULI ertelmezes (host-tesztelheto). Hibas JSON-ra nem omlik ossze.
// Visszaad: None, ha legalabb a session- vagy a heti limit megvan; kulonben Parse.
FetchError parseUsageUngated(const char *body, size_t len, UsageData &out, UsageShape &shape);

// A firmware ezt hivja: parseUsageUngated + kapu.
FetchError parseUsage(const String &body, UsageData &out);
