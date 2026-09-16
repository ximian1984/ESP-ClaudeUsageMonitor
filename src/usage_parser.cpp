#include "usage_parser.h"

#include <ArduinoJson.h>

#include "time_manager.h"

// Forrasok (PLAN.md 2.5) — egyik sem a mi mert valaszunk:
//  A) github.com/linuxlewis/claude-usage @ ac15351 (2026-02-21):
//     ClaudeUsage/Models/UsageData.swift:3-33 (utilization Double, resets_at Date?, five_hour/seven_day kotelezo,
//     seven_day_sonnet/opus/oauth_apps/cowork, iguana_necktie, extra_usage opcionalis),
//     ClaudeUsageTests/ClaudeUsageTests.swift:35-42 (minta: "resets_at":"2026-02-08T18:59:59.661633+00:00"),
//     ClaudeUsage/Views/UsageBar.swift:8 (utilization 0..100 skala).
//  B) Chrome-extension leiras (a koordinator kozvetitese, URL nelkul, NEM ellenorzott):
//     "raw_limits": {"five_hour": {"remaining_tokens","total_tokens","reset_at"}, "seven_day": {...}}

static bool readLimit(JsonObjectConst o, UsageLimit &l) {
  if (o["utilization"].is<float>()) {
    l.hasUtilization = true;
    l.utilizationPct = o["utilization"].as<float>();  // A: mar szazalek (0..100)
  } else if (o["remaining_tokens"].is<double>() && o["total_tokens"].is<double>()) {
    double total = o["total_tokens"].as<double>();
    double rem = o["remaining_tokens"].as<double>();
    if (total > 0) {
      l.hasUtilization = true;
      l.utilizationPct = (float)(100.0 * (1.0 - rem / total));
    }
  }
  const char *reset = o["resets_at"].is<const char *>() ? o["resets_at"].as<const char *>()
                      : o["reset_at"].is<const char *>() ? o["reset_at"].as<const char *>()
                                                         : nullptr;
  time_t t;
  if (reset && TimeManager::parseIso8601(reset, t)) {
    l.hasReset = true;
    l.resetAt = t;
  }
  if (l.hasUtilization) {
    if (l.utilizationPct < 0) l.utilizationPct = 0;
    if (l.utilizationPct > 100) l.utilizationPct = 100;
  }
  return l.hasUtilization || l.hasReset;
}

static void addLimit(UsageData &out, LimitKind kind, const char *label, JsonVariantConst v) {
  if (out.count >= MAX_LIMITS || !v.is<JsonObjectConst>()) return;  // null / nem objektum: nincs ilyen limit
  UsageLimit l;
  l.kind = kind;
  strlcpy(l.label, label, sizeof(l.label));
  if (readLimit(v.as<JsonObjectConst>(), l)) out.limits[out.count++] = l;
}

FetchError parseUsageUngated(const char *body, size_t len, UsageData &out, UsageShape &shape) {
  out = UsageData();
  shape = UsageShape::None;
  if (!body || len == 0) return FetchError::Parse;

  JsonDocument doc;
  if (deserializeJson(doc, body, len)) return FetchError::Parse;
  if (!doc.is<JsonObjectConst>()) return FetchError::Parse;

  JsonObjectConst root = doc.as<JsonObjectConst>();
  JsonObjectConst limits = root;
  if (root["raw_limits"].is<JsonObjectConst>()) {
    limits = root["raw_limits"].as<JsonObjectConst>();
    shape = UsageShape::RawLimits;
  } else {
    shape = UsageShape::TopLevel;
  }

  // Elobb a ket fo limit (a kijelzo ezeket keresi), utana a tobbi, amig van hely.
  addLimit(out, LimitKind::Session, "SESSION", limits["five_hour"]);
  addLimit(out, LimitKind::Weekly, "WEEKLY", limits["seven_day"]);
  for (JsonPairConst kv : limits) {
    const char *key = kv.key().c_str();
    if (!strcmp(key, "five_hour") || !strcmp(key, "seven_day")) continue;
    char label[sizeof(UsageLimit::label)];
    if (!strncmp(key, "seven_day_", 10)) {
      snprintf(label, sizeof(label), "7D %s", key + 10);
    } else {
      strlcpy(label, key, sizeof(label));
    }
    for (char *p = label; *p; p++) *p = (*p == '_') ? ' ' : (char)toupper((unsigned char)*p);
    addLimit(out, LimitKind::Other, label, kv.value());  // ismeretlen kulcs: ha limit-alaku, felveszi; kulonben kihagyja
  }

  if (!out.find(LimitKind::Session) && !out.find(LimitKind::Weekly)) {
    out = UsageData();
    shape = UsageShape::None;
    return FetchError::Parse;
  }
  return FetchError::None;
}

FetchError parseUsage(const String &body, UsageData &out) {
  UsageShape shape;
  FetchError err = parseUsageUngated(body.c_str(), body.length(), out, shape);
  // Csak az alak es a limitek szama kerul logba (ertek nem).
  Serial.printf("[parser] alak=%s, limitek=%u, eredmeny=%s\n",
                shape == UsageShape::TopLevel ? "A/top-level" : shape == UsageShape::RawLimits ? "B/raw_limits" : "ismeretlen",
                (unsigned)out.count, fetchErrorTitle(err));
#if !defined(USAGE_PARSER_ENABLE) || USAGE_PARSER_ENABLE == 0
  if (err == FetchError::None) {
    out = UsageData();
    return FetchError::ParserPending;  // ⛔ kapu: a valos minta igazolasaig nincs kiadott adat
  }
#endif
  return err;
}
