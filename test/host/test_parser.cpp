// Host-teszt: usage_parser. A mintak forrasa:
//  A) github.com/linuxlewis/claude-usage @ ac15351 — ClaudeUsageTests/ClaudeUsageTests.swift:35-42 es SPEC.md:13-21
//  B) a koordinator altal kozvetitett Chrome-extension leiras (URL nelkul, NEM ellenorzott) — a mezonevek csak onnan
// Egyik sem a mi mert claude.ai-valaszunk.
#include "usage_parser.h"
#include "time_manager.h"
#include <cstdio>

int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static FetchError P(const char *s, UsageData &d, UsageShape &sh) { return parseUsageUngated(s, strlen(s), d, sh); }

int main() {
  UsageData d;
  UsageShape sh;
  time_t t;

  // A — linuxlewis teszt-minta, szo szerint
  const char *A_TEST = R"({
    "five_hour": {"utilization": 17.0, "resets_at": "2026-02-08T18:59:59.661633+00:00"},
    "seven_day": {"utilization": 11.0, "resets_at": "2026-02-14T16:59:59.661657+00:00"},
    "seven_day_oauth_apps": null,
    "seven_day_opus": null,
    "seven_day_sonnet": {"utilization": 0.0, "resets_at": null},
    "seven_day_cowork": null,
    "iguana_necktie": null,
    "extra_usage": null
  })";
  CHECK(P(A_TEST, d, sh) == FetchError::None);
  CHECK(sh == UsageShape::TopLevel);
  CHECK(d.count == 3);
  const UsageLimit *s = d.find(LimitKind::Session);
  const UsageLimit *w = d.find(LimitKind::Weekly);
  CHECK(s && s->hasUtilization && s->utilizationPct == 17.0f && s->hasReset);
  CHECK(TimeManager::parseIso8601("2026-02-08T18:59:59Z", t) && s && s->resetAt == t);
  CHECK(w && w->utilizationPct == 11.0f && w->hasReset);
  CHECK(!strcmp(d.limits[2].label, "7D SONNET") && d.limits[2].kind == LimitKind::Other && !d.limits[2].hasReset);

  // A — linuxlewis SPEC.md minta ("Z", egesz szamok nelkul, "..." ervenytelen ido)
  const char *A_SPEC = R"({"five_hour":{"utilization":17.0,"resets_at":"2026-02-08T18:59:59Z"},
    "seven_day":{"utilization":11.0,"resets_at":"2026-02-14T16:59:59Z"},
    "seven_day_sonnet":{"utilization":0.0,"resets_at":null},
    "seven_day_opus":{"utilization":5.0,"resets_at":"..."},
    "seven_day_oauth_apps":null,"seven_day_cowork":null,"extra_usage":null})";
  CHECK(P(A_SPEC, d, sh) == FetchError::None);
  CHECK(d.count == 4);
  CHECK(!strcmp(d.limits[3].label, "7D OPUS") && d.limits[3].hasUtilization && !d.limits[3].hasReset);

  // Egesz szam utilization
  CHECK(P(R"({"five_hour":{"utilization":42,"resets_at":null}})", d, sh) == FetchError::None);
  CHECK(d.count == 1 && d.limits[0].utilizationPct == 42.0f);

  // B — raw_limits (leiras alapjan)
  const char *B = R"({"raw_limits":{"five_hour":{"remaining_tokens":750,"total_tokens":1000,"reset_at":"2026-09-16T20:00:00Z"},
                                    "seven_day":{"remaining_tokens":0,"total_tokens":5000,"reset_at":"2026-09-20T08:00:00Z"}}})";
  CHECK(P(B, d, sh) == FetchError::None);
  CHECK(sh == UsageShape::RawLimits);
  s = d.find(LimitKind::Session);
  w = d.find(LimitKind::Weekly);
  CHECK(s && s->utilizationPct == 25.0f && s->hasReset);
  CHECK(w && w->utilizationPct == 100.0f);

  // B — total 0: nincs szazalek, de reset van
  CHECK(P(R"({"raw_limits":{"five_hour":{"remaining_tokens":0,"total_tokens":0,"reset_at":"2026-09-16T20:00:00Z"}}})", d, sh) == FetchError::None);
  CHECK(d.count == 1 && !d.limits[0].hasUtilization && d.limits[0].hasReset);

  // Tartomanyon kivuli ertek vagasa
  CHECK(P(R"({"five_hour":{"utilization":130.5},"seven_day":{"utilization":-3}})", d, sh) == FetchError::None);
  CHECK(d.limits[0].utilizationPct == 100.0f && d.limits[1].utilizationPct == 0.0f);

  // Ismeretlen extra kulcsok nem zavarnak; nem-limit kulcs kimarad
  CHECK(P(R"({"five_hour":{"utilization":1},"something":"x","nested":{"foo":1},"new_limit":{"utilization":9,"resets_at":null}})", d, sh) == FetchError::None);
  CHECK(d.count == 2 && !strcmp(d.limits[1].label, "NEW LIMIT"));

  // Sok limit: MAX_LIMITS-nel megall, a fo limitek elol
  CHECK(P(R"({"a1":{"utilization":1},"a2":{"utilization":1},"a3":{"utilization":1},"a4":{"utilization":1},"a5":{"utilization":1},
             "a6":{"utilization":1},"seven_day":{"utilization":2},"five_hour":{"utilization":3}})", d, sh) == FetchError::None);
  CHECK(d.count == MAX_LIMITS && d.limits[0].kind == LimitKind::Session && d.limits[1].kind == LimitKind::Weekly);

  // Hibas / nem felismert bemenetek -> Parse, osszeomlas nelkul
  const char *bad[] = {"", "not json", "{", "[]", "null", "42", R"({"five_hour":null,"seven_day":null})",
                       R"({"type":"error","error":{"type":"permission_error","message":"Invalid authorization"}})",
                       R"({"five_hour":{"utilization":"17"}})", R"({"raw_limits":{}})",
                       R"({"five_hour":{"utilization":17.0,"resets_at":"2026-02-08T18:59:59Z")"};
  for (const char *b : bad) {
    FetchError e = P(b, d, sh);
    CHECK(e == FetchError::Parse);
    CHECK(d.count == 0 && sh == UsageShape::None);
  }
  CHECK(parseUsageUngated(nullptr, 0, d, sh) == FetchError::Parse);

  // A kapu: alapbuildben felismert alak -> ParserPending, adat nelkul
  FetchError g = parseUsage(String(A_TEST), d);
#if defined(USAGE_PARSER_ENABLE) && USAGE_PARSER_ENABLE
  CHECK(g == FetchError::None && d.count == 3);
#else
  CHECK(g == FetchError::ParserPending && d.count == 0);
#endif
  CHECK(parseUsage(String("garbage"), d) == FetchError::Parse);

  printf("parser fails=%d\n", fails);
  return fails;
}
