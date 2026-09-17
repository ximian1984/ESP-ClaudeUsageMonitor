#include "usage_parser.h"

#include <ArduinoJson.h>

#include "time_manager.h"

// Forras: VALOS valasz, api.anthropic.com/api/oauth/usage, HTTP 200, 2026-09-16 (a koordinator merese),
// test/host/fixtures/usage_oauth_2026-09-16.json. Mezok onnan:
//   limits[]: {"kind":"session"|"weekly_all"|"weekly_scoped", "group":"session"|"weekly", "percent":26,
//              "severity":"normal"|"warning", "resets_at":"2026-09-16T18:40:00.595003+00:00",
//              "scope":null | {"model":{"id":null,"display_name":"Fable"},"surface":null}, "is_active":bool}
//   five_hour / seven_day: {"utilization":26.0, "resets_at":"...+00:00", "limit_dollars":null, ...}
// A mintaban limits[].percent == five_hour/seven_day.utilization (26/26, 81/81) -> ugyanaz a 0..100 skala.

static bool readNumber(JsonVariantConst v, float &out) {
  if (!v.is<float>()) return false;  // ArduinoJson 7: egesz szamra is igaz; null/string -> hamis
  out = v.as<float>();
  if (out < 0) out = 0;
  if (out > 100) out = 100;
  return true;
}

static bool readReset(JsonVariantConst v, time_t &out) {
  return v.is<const char *>() && TimeManager::parseIso8601(v.as<const char *>(), out);
}

static Severity readSeverity(JsonVariantConst v) {
  if (!v.is<const char *>()) return Severity::None;
  const char *s = v.as<const char *>();
  if (!strcmp(s, "normal")) return Severity::Normal;
  if (!strcmp(s, "warning")) return Severity::Warning;
  return Severity::Unknown;
}

static void upperLabel(char *dst, size_t n, const char *prefix, const char *text) {
  snprintf(dst, n, "%s%s", prefix, text);
  for (char *p = dst; *p; p++) *p = (*p == '_') ? ' ' : (char)toupper((unsigned char)*p);
}

static void push(UsageData &out, const UsageLimit &l) {
  if (out.count < MAX_LIMITS) out.limits[out.count++] = l;
}

// 1) limits[] — a session es weekly_all elore kerul, a tobbi utana.
static void fromLimitsArray(JsonArrayConst arr, UsageData &out) {
  for (int pass = 0; pass < 2; pass++) {
    for (JsonObjectConst o : arr) {
      const char *kind = o["kind"].is<const char *>() ? o["kind"].as<const char *>() : nullptr;
      if (!kind) continue;
      UsageLimit l;
      if (!strcmp(kind, "session")) {
        l.kind = LimitKind::Session;
        strlcpy(l.label, "SESSION", sizeof(l.label));
      } else if (!strcmp(kind, "weekly_all")) {
        l.kind = LimitKind::Weekly;
        strlcpy(l.label, "WEEKLY", sizeof(l.label));
      } else if (!strcmp(kind, "weekly_scoped")) {
        l.kind = LimitKind::Other;
        const char *model = o["scope"]["model"]["display_name"].is<const char *>()
                                ? o["scope"]["model"]["display_name"].as<const char *>()
                                : "SCOPED";
        upperLabel(l.label, sizeof(l.label), "7D ", model);
      } else {
        l.kind = LimitKind::Other;  // ismeretlen kind: megtartjuk, de nem talalgatjuk a jelenteset
        upperLabel(l.label, sizeof(l.label), "", kind);
      }
      bool primary = l.kind != LimitKind::Other;
      if (primary != (pass == 0)) continue;
      if (l.kind != LimitKind::Other && out.find(l.kind)) continue;  // duplikalt fo limit: az elso szamit

      l.hasUtilization = readNumber(o["percent"], l.utilizationPct);
      l.hasReset = readReset(o["resets_at"], l.resetAt);
      l.severity = readSeverity(o["severity"]);
      l.isActive = o["is_active"].is<bool>() && o["is_active"].as<bool>();
      if (l.hasUtilization || l.hasReset) push(out, l);
    }
  }
}

// 2) Tartalek: five_hour / seven_day, ha a limits[] nem adta meg.
static void fromTopLevel(JsonObjectConst root, const char *key, LimitKind kind, const char *label, UsageData &out) {
  if (out.find(kind) || !root[key].is<JsonObjectConst>()) return;
  JsonObjectConst o = root[key].as<JsonObjectConst>();
  UsageLimit l;
  l.kind = kind;
  strlcpy(l.label, label, sizeof(l.label));
  l.hasUtilization = readNumber(o["utilization"], l.utilizationPct);
  l.hasReset = readReset(o["resets_at"], l.resetAt);
  if (!(l.hasUtilization || l.hasReset)) return;
  // A fo limitek maradjanak elol (a kijelzo es a MAX_LIMITS-vagas miatt).
  if (out.count >= MAX_LIMITS) out.count = MAX_LIMITS - 1;
  int pos = (kind == LimitKind::Session) ? 0 : (out.find(LimitKind::Session) ? 1 : 0);
  for (int i = out.count; i > pos; i--) out.limits[i] = out.limits[i - 1];
  out.limits[pos] = l;
  out.count++;
}

FetchError parseUsageUngated(const char *body, size_t len, UsageData &out, UsageSource &source) {
  out = UsageData();
  source = UsageSource::None;
  if (!body || len == 0) return FetchError::Parse;

  JsonDocument doc;
  if (deserializeJson(doc, body, len)) return FetchError::Parse;
  if (!doc.is<JsonObjectConst>()) return FetchError::Parse;
  JsonObjectConst root = doc.as<JsonObjectConst>();

  if (root["limits"].is<JsonArrayConst>()) fromLimitsArray(root["limits"].as<JsonArrayConst>(), out);
  bool fromArray = out.find(LimitKind::Session) || out.find(LimitKind::Weekly);
  fromTopLevel(root, "five_hour", LimitKind::Session, "SESSION", out);
  fromTopLevel(root, "seven_day", LimitKind::Weekly, "WEEKLY", out);

  if (!out.find(LimitKind::Session) && !out.find(LimitKind::Weekly)) {
    out = UsageData();
    return FetchError::Parse;
  }
  source = fromArray ? UsageSource::LimitsArray : UsageSource::TopLevel;
  return FetchError::None;
}

// ---------------------------------------------------------------------------------------------
// Uj szolgaltatok
// ---------------------------------------------------------------------------------------------

// Kijelzo-helyre tesz: az elso ket limit a Session/Weekly "slot" (a kijelzo ezt a ketto rajzolja), a tobbi Other.
static void pushSlot(UsageData &out, UsageLimit l) {
  if (!out.find(LimitKind::Session)) l.kind = LimitKind::Session;
  else if (!out.find(LimitKind::Weekly)) l.kind = LimitKind::Weekly;
  else l.kind = LimitKind::Other;
  push(out, l);
}

// "gemini-2.5-pro" -> "2.5 PRO"; tul hosszu -> vagva (a cimke 11 karakter).
static void geminiLabel(char *dst, size_t n, const char *modelId) {
  const char *m = modelId ? modelId : "";
  if (!strncmp(m, "gemini-", 7)) m += 7;
  upperLabel(dst, n, "", m);
  for (char *p = dst; *p; p++)
    if (*p == '-') *p = ' ';
}

static int geminiRank(const char *modelId) {  // stabil sorrend: pro elore, aztan flash, aztan a tobbi
  if (!modelId) return 9;
  if (strstr(modelId, "pro")) return 0;
  if (strstr(modelId, "flash")) return 1;
  return 2;
}

static void fromGemini(JsonObjectConst root, UsageData &out) {
  JsonArrayConst buckets = root["buckets"].as<JsonArrayConst>();
  // Ket menet a stabil sorrendhez (rank 0..2); bucketonkent egy limit.
  for (int rank = 0; rank <= 2; rank++) {
    for (JsonObjectConst b : buckets) {
      const char *model = b["modelId"].is<const char *>() ? b["modelId"].as<const char *>() : nullptr;
      if (geminiRank(model) != rank) continue;
      UsageLimit l;
      geminiLabel(l.label, sizeof(l.label), model ? model : "QUOTA");
      if (b["remainingFraction"].is<float>()) {
        float rem = b["remainingFraction"].as<float>();
        if (rem < 0) rem = 0;
        if (rem > 1) rem = 1;
        l.utilizationPct = (1.0f - rem) * 100.0f;
        l.hasUtilization = true;
      }
      l.hasReset = readReset(b["resetTime"], l.resetAt);
      if (l.hasUtilization || l.hasReset) pushSlot(out, l);
    }
  }
}

static void codexWindow(JsonObjectConst w, UsageData &out) {
  if (w.isNull()) return;
  UsageLimit l;
  long secs = w["limit_window_seconds"] | 0L;
  if (secs > 0 && secs <= 86400) strlcpy(l.label, secs == 18000 ? "5H WINDOW" : "SESSION", sizeof(l.label));
  else if (secs == 604800) strlcpy(l.label, "WEEKLY", sizeof(l.label));
  else if (secs > 0) snprintf(l.label, sizeof(l.label), "%uD WINDOW", (unsigned)((secs / 86400) % 100));
  else strlcpy(l.label, "LIMIT", sizeof(l.label));
  l.hasUtilization = readNumber(w["used_percent"], l.utilizationPct);
  long resetAt = w["reset_at"] | 0L;  // epoch (rate_limit_window_snapshot.rs: i32)
  if (resetAt > 0) {
    l.resetAt = (time_t)resetAt;
    l.hasReset = true;
  }
  if (l.hasUtilization || l.hasReset) pushSlot(out, l);
}

static void fromCodex(JsonObjectConst root, UsageData &out) {
  JsonObjectConst rl = root["rate_limit"].as<JsonObjectConst>();
  if (rl.isNull()) return;
  codexWindow(rl["primary_window"].as<JsonObjectConst>(), out);
  codexWindow(rl["secondary_window"].as<JsonObjectConst>(), out);
}

static void fromGrok(JsonObjectConst root, UsageData &out) {
  JsonObjectConst c = root["config"].as<JsonObjectConst>();
  if (c.isNull()) return;
  UsageLimit l;
  strlcpy(l.label, "CREDITS", sizeof(l.label));
  if (c["creditUsagePercent"].is<float>()) {
    l.hasUtilization = readNumber(c["creditUsagePercent"], l.utilizationPct);
  } else if (c["onDemandCap"]["val"].is<float>() && c["onDemandUsed"]["val"].is<float>()) {
    float cap = c["onDemandCap"]["val"].as<float>(), used = c["onDemandUsed"]["val"].as<float>();
    if (cap > 0) {
      float pct = used / cap * 100.0f;
      l.utilizationPct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
      l.hasUtilization = true;
    }
  }
  // GrokCreditsProxyFetcher.swift:58-60: a currentPeriod.end az elsodleges, kulonben billingPeriodEnd
  l.hasReset = readReset(c["currentPeriod"]["end"], l.resetAt) || readReset(c["billingPeriodEnd"], l.resetAt);
  if (l.hasUtilization || l.hasReset) pushSlot(out, l);
}

FetchError parseProviderUngated(ClaudeTransport transport, const char *body, size_t len, UsageData &out, UsageSource &source) {
  out = UsageData();
  source = UsageSource::None;
  if (!body || len == 0) return FetchError::Parse;
  JsonDocument doc;
  if (deserializeJson(doc, body, len) || !doc.is<JsonObjectConst>()) return FetchError::Parse;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  switch (transport) {
    case ClaudeTransport::Gemini:
      fromGemini(root, out);
      source = UsageSource::GeminiBuckets;
      break;
    case ClaudeTransport::ChatGpt:
      fromCodex(root, out);
      source = UsageSource::CodexRateLimit;
      break;
    case ClaudeTransport::Grok:
      fromGrok(root, out);
      source = UsageSource::GrokCredits;
      break;
    default:
      return parseUsageUngated(body, len, out, source);
  }
  if (out.count == 0) {
    source = UsageSource::None;
    return FetchError::Parse;
  }
  return FetchError::None;
}

static const char *sourceName(UsageSource s) {
  switch (s) {
    case UsageSource::LimitsArray: return "limits[]";
    case UsageSource::TopLevel: return "five_hour/seven_day";
    case UsageSource::GeminiBuckets: return "gemini buckets[]";
    case UsageSource::CodexRateLimit: return "chatgpt rate_limit";
    case UsageSource::GrokCredits: return "grok credits";
    default: return "ismeretlen";
  }
}

FetchError parseUsage(ClaudeTransport transport, const String &body, UsageData &out) {
  UsageSource source;
  bool claude = transport == ClaudeTransport::OAuth || transport == ClaudeTransport::WebSession;
  FetchError err = claude ? parseUsageUngated(body.c_str(), body.length(), out, source)
                          : parseProviderUngated(transport, body.c_str(), body.length(), out, source);
  // Csak a forras es a limitek szama kerul logba (ertek nem).
  Serial.printf("[parser] forras=%s, limitek=%u, eredmeny=%s\n", sourceName(source), (unsigned)out.count,
                err == FetchError::None ? "OK" : fetchErrorTitle(err));
#if USAGE_PARSER_ENABLE == 0
  if (claude && err == FetchError::None) {
    out = UsageData();
    return FetchError::ParserPending;
  }
#endif
  return err;
}
