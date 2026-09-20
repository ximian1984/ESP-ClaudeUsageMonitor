// Host-teszt: usage_parser.
// Elsodleges minta: egy VALOS valasz ALAKJA, api.anthropic.com/api/oauth/usage, 2026-09-16,
//   fixtures/usage_oauth_2026-09-16.json. A szerkezet szo szerinti, a szamok KITALALTAK (nincs benne
//   valos kvota-adat es nincs PII).
// Masodlagos: github.com/linuxlewis/claude-usage @ ac15351, ClaudeUsageTests.swift:35-42 (csak top-level alak).
#include "usage_parser.h"
#include "time_manager.h"
#include <cstdio>
#include <fstream>
#include <sstream>

int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static FetchError P(const std::string &s, UsageData &d, UsageSource &src) { return parseUsageUngated(s.c_str(), s.size(), d, src); }
static time_t iso(const char *s) { time_t t = 0; TimeManager::parseIso8601(s, t); return t; }

int main() {
  UsageData d;
  UsageSource src;

  // --- 1. VALOS minta ---
  std::ifstream f("fixtures/usage_oauth_2026-09-16.json");
  std::stringstream ss;
  ss << f.rdbuf();
  std::string real = ss.str();
  CHECK(real.size() == 3179);
  CHECK(P(real, d, src) == FetchError::None);
  CHECK(src == UsageSource::LimitsArray);
  CHECK(d.count == 3);
  const UsageLimit *s = d.find(LimitKind::Session);
  const UsageLimit *w = d.find(LimitKind::Weekly);
  CHECK(s && s->hasUtilization && s->utilizationPct == 34.0f);
  CHECK(s && s->hasReset && s->resetAt == iso("2026-09-16T18:40:00Z"));
  CHECK(s && s->severity == Severity::Normal && !s->isActive);
  CHECK(w && w->utilizationPct == 68.0f && w->resetAt == iso("2026-09-18T07:00:00Z"));
  CHECK(w && w->severity == Severity::Warning && w->isActive);
  CHECK(!strcmp(d.limits[2].label, "7D FABLE") && d.limits[2].kind == LimitKind::Other && d.limits[2].utilizationPct == 0.0f);
  CHECK(d.limits[0].kind == LimitKind::Session && d.limits[1].kind == LimitKind::Weekly);

  // --- 2. Ugyanaz a valos minta limits[] NELKUL -> a five_hour/seven_day tartalek ugyanazt adja ---
  {
    std::string noLimits = real;
    size_t a = noLimits.find("\"limits\"");
    CHECK(a != std::string::npos);
    noLimits.replace(a, 8, "\"limitsX\"");
    CHECK(P(noLimits, d, src) == FetchError::None);
    CHECK(src == UsageSource::TopLevel);
    CHECK(d.count == 2);
    s = d.find(LimitKind::Session);
    w = d.find(LimitKind::Weekly);
    CHECK(s && s->utilizationPct == 34.0f && s->resetAt == iso("2026-09-16T18:40:00Z") && s->severity == Severity::None);
    CHECK(w && w->utilizationPct == 68.0f && w->resetAt == iso("2026-09-18T07:00:00Z"));
  }

  // --- 3. linuxlewis teszt-minta (top-level, limits nelkul) ---
  CHECK(P(R"({"five_hour": {"utilization": 17.0, "resets_at": "2026-02-08T18:59:59.661633+00:00"},
    "seven_day": {"utilization": 11.0, "resets_at": "2026-02-14T16:59:59.661657+00:00"},
    "seven_day_oauth_apps": null, "seven_day_opus": null,
    "seven_day_sonnet": {"utilization": 0.0, "resets_at": null},
    "seven_day_cowork": null, "iguana_necktie": null, "extra_usage": null})", d, src) == FetchError::None);
  CHECK(src == UsageSource::TopLevel && d.count == 2 && d.find(LimitKind::Session)->utilizationPct == 17.0f);

  // --- 4. Vegyes: limits[] csak weekly-t ad -> session a tartalekbol, a fo limitek elol ---
  CHECK(P(R"({"five_hour":{"utilization":5,"resets_at":null},
    "limits":[{"kind":"weekly_scoped","percent":3,"scope":{"model":{"display_name":"x_y"}}},
              {"kind":"weekly_all","percent":40,"severity":"critical","resets_at":"2026-09-18T07:00:00+00:00"}]})", d, src) == FetchError::None);
  CHECK(src == UsageSource::LimitsArray && d.count == 3);
  CHECK(d.limits[0].kind == LimitKind::Session && d.limits[0].utilizationPct == 5.0f);
  CHECK(d.limits[1].kind == LimitKind::Weekly && d.limits[1].severity == Severity::Unknown);
  CHECK(!strcmp(d.limits[2].label, "7D X Y"));

  // --- 5. Ismeretlen kind megmarad Other-kent; scope nelkuli weekly_scoped; vagas 0..100 ---
  CHECK(P(R"({"limits":[{"kind":"session","percent":130},{"kind":"monthly_new","percent":-2},{"kind":"weekly_scoped","percent":1},{"percent":9}]})", d, src) == FetchError::None);
  CHECK(d.count == 3 && d.limits[0].utilizationPct == 100.0f);
  CHECK(!strcmp(d.limits[1].label, "MONTHLY NEW") && d.limits[1].utilizationPct == 0.0f);
  CHECK(!strcmp(d.limits[2].label, "7D SCOPED"));

  // --- 6. Sok limit: MAX_LIMITS, fo limitek elol akkor is, ha a tartalekbol jonnek ---
  CHECK(P(R"({"limits":[{"kind":"a","percent":1},{"kind":"b","percent":1},{"kind":"c","percent":1},{"kind":"d","percent":1},
    {"kind":"e","percent":1},{"kind":"f","percent":1},{"kind":"g","percent":1}],
    "five_hour":{"utilization":7},"seven_day":{"utilization":8}})", d, src) == FetchError::None);
  CHECK(d.count == MAX_LIMITS && d.limits[0].kind == LimitKind::Session && d.limits[1].kind == LimitKind::Weekly);
  CHECK(src == UsageSource::TopLevel);

  // --- 7. Duplikalt session: az elso szamit ---
  CHECK(P(R"({"limits":[{"kind":"session","percent":10},{"kind":"session","percent":90}]})", d, src) == FetchError::None);
  CHECK(d.count == 1 && d.limits[0].utilizationPct == 10.0f);

  // --- 8. Hibas / nem felismert bemenetek -> Parse, osszeomlas nelkul ---
  const char *bad[] = {"", "not json", "{", "[]", "null", "42", R"({"five_hour":null,"seven_day":null})",
                       R"({"type":"error","error":{"type":"authentication_error","message":"OAuth access token is invalid."},"request_id":null})",
                       R"({"five_hour":{"utilization":"17"}})", R"({"limits":[]})", R"({"limits":{"kind":"session"}})",
                       R"({"limits":[{"kind":"session","percent":null,"resets_at":null}]})",
                       R"({"raw_limits":{"five_hour":{"remaining_tokens":1,"total_tokens":2}}})",
                       R"({"five_hour":{"utilization":17.0,"resets_at":"2026-02-08T18:59:59Z")"};
  for (const char *b : bad) {
    CHECK(P(b, d, src) == FetchError::Parse);
    CHECK(d.count == 0 && src == UsageSource::None);
  }
  CHECK(parseUsageUngated(nullptr, 0, d, src) == FetchError::Parse);

  // --- 9. A kapu ---
#ifndef EXPECT_GATE_CLOSED
  CHECK(USAGE_PARSER_ENABLE == 1);  // alapbuildben a kapu NYITVA (az alak valos mintaval igazolt)
#endif
  FetchError g = parseUsage(ClaudeTransport::OAuth, String(real.c_str()), d);
#if USAGE_PARSER_ENABLE == 0
  CHECK(g == FetchError::ParserPending && d.count == 0);
#else
  CHECK(g == FetchError::None && d.count == 3);
#endif
  CHECK(parseUsage(ClaudeTransport::OAuth, String("garbage"), d) == FetchError::Parse);

  printf("parser fails=%d\n", fails);
  return fails;
}
