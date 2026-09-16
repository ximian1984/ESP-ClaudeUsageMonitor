// JSON-valasz -> UsageData.
//
// ⛔ BLOKKOLVA: a valos, hitelesitett /usage valasz szerkezete meg NINCS merve (PLAN.md 2.4).
// Mezoneveket kitalalni tilos (spec 5., 25.). Amig a projektgazda mintaja nem jon meg, a parseUsage()
// ParserPending-et ad, es a kijelzo ezt mutatja — nem hamis szamot.
#pragma once
#include <Arduino.h>

#include "usage_types.h"

// body: a teljes valasz-body (legfeljebb CLAUDE_MAX_BODY_BYTES). Hibas JSON-ra nem omlik ossze.
FetchError parseUsage(const String &body, UsageData &out);
