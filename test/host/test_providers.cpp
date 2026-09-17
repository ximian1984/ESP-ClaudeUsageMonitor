// Host-teszt: Gemini / ChatGPT / Grok parser + JWT account-ID + base64url-dekodolas.
// ⚠ A mintak SZINTETIKUSAK: a mezoneveket es tipusokat a FORRASBOL vettuk (PLAN 2.13/b), valos valasz meg nincs.
//   Gemini : gemini-cli packages/core/src/code_assist/types.ts:250-265 (BucketInfo)
//   ChatGPT: codex codex-backend-openapi-models rate_limit_status_payload.rs / _details.rs / _window_snapshot.rs
//   Grok   : CodexBar GrokCreditsProxyFetcher.swift:44-120 (CreditsResponse)
// Az elvart idopontokat python datetime-mal szamoltuk (lasd a megjegyzeseket).
#include "base64url.h"
#include "jwt_util.h"
#include "usage_parser.h"
#include <cmath>
int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }
static FetchError P(ClaudeTransport t, const std::string &s, UsageData &d) {
  UsageSource src;
  return parseProviderUngated(t, s.c_str(), s.size(), d, src);
}

int main() {
  UsageData d;
  // ---- Gemini: ket bucket, rank szerint pro elore (a sorrendtol fuggetlenul), a harmadik Other
  // 2026-09-18T00:00:00Z = 1789689600
  std::string gem = R"({"buckets":[
    {"modelId":"gemini-2.5-flash","tokenType":"REQUESTS","remainingFraction":0.9,"resetTime":"2026-09-18T00:00:00Z","remainingAmount":"900"},
    {"modelId":"gemini-2.5-pro","tokenType":"REQUESTS","remainingFraction":0.25,"resetTime":"2026-09-18T00:00:00Z"},
    {"modelId":"gemini-2.0-flash-lite","remainingFraction":1}]})";
  CHECK(P(ClaudeTransport::Gemini, gem, d) == FetchError::None);
  CHECK(d.count == 3);
  const UsageLimit *s = d.find(LimitKind::Session), *w = d.find(LimitKind::Weekly);
  CHECK(s && std::string(s->label) == "2.5 PRO" && near(s->utilizationPct, 75) && s->resetAt == 1789689600);
  CHECK(w && std::string(w->label) == "2.5 FLASH" && near(w->utilizationPct, 10));
  CHECK(d.limits[2].kind == LimitKind::Other && std::string(d.limits[2].label) == "2.0 FLASH L");  // 11 karakterre vagva
  CHECK(P(ClaudeTransport::Gemini, R"({"buckets":[]})", d) == FetchError::Parse);
  CHECK(P(ClaudeTransport::Gemini, R"({"buckets":[{"modelId":"x","remainingFraction":1.7}]})", d) == FetchError::None && near(d.limits[0].utilizationPct, 0));
  CHECK(P(ClaudeTransport::Gemini, "not json", d) == FetchError::Parse);

  // ---- ChatGPT (Codex): 5 oras + heti ablak, reset_at epoch
  std::string cx = R"({"plan_type":"plus","rate_limit":{"allowed":true,"limit_reached":false,
    "primary_window":{"used_percent":42,"limit_window_seconds":18000,"reset_after_seconds":3600,"reset_at":1789660800},
    "secondary_window":{"used_percent":7,"limit_window_seconds":604800,"reset_after_seconds":500000,"reset_at":1790160000}},
    "credits":null,"additional_rate_limits":null})";
  CHECK(P(ClaudeTransport::ChatGpt, cx, d) == FetchError::None);
  s = d.find(LimitKind::Session);
  w = d.find(LimitKind::Weekly);
  CHECK(s && std::string(s->label) == "5H WINDOW" && near(s->utilizationPct, 42) && s->resetAt == 1789660800);
  CHECK(w && std::string(w->label) == "WEEKLY" && near(w->utilizationPct, 7) && w->resetAt == 1790160000);
  CHECK(P(ClaudeTransport::ChatGpt, R"({"plan_type":"free","rate_limit":null})", d) == FetchError::Parse);
  CHECK(P(ClaudeTransport::ChatGpt, R"({"rate_limit":{"primary_window":{"used_percent":150,"limit_window_seconds":259200,"reset_at":1}}})", d) == FetchError::None);
  CHECK(std::string(d.limits[0].label) == "3D WINDOW" && near(d.limits[0].utilizationPct, 100));

  // ---- Grok: creditUsagePercent + currentPeriod.end; tartalek: onDemand + billingPeriodEnd
  // 2026-10-01T00:00:00Z = 1790812800
  CHECK(P(ClaudeTransport::Grok, R"({"config":{"creditUsagePercent":33.5,"currentPeriod":{"start":"2026-09-01T00:00:00Z","end":"2026-10-01T00:00:00Z"}}})", d) == FetchError::None);
  CHECK(d.count == 1 && std::string(d.limits[0].label) == "CREDITS" && near(d.limits[0].utilizationPct, 33.5) && d.limits[0].resetAt == 1790812800);
  CHECK(d.find(LimitKind::Session) && !d.find(LimitKind::Weekly));
  CHECK(P(ClaudeTransport::Grok, R"({"config":{"onDemandCap":{"val":20},"onDemandUsed":{"val":5},"billingPeriodEnd":"2026-10-01T00:00:00Z"}})", d) == FetchError::None);
  CHECK(near(d.limits[0].utilizationPct, 25) && d.limits[0].resetAt == 1790812800);
  CHECK(P(ClaudeTransport::Grok, R"({"subscriptionTier":"x"})", d) == FetchError::Parse);

  // ---- a Claude-ut valtozatlan marad a dispatcheren at
  CHECK(P(ClaudeTransport::OAuth, R"({"limits":[{"kind":"session","percent":26,"resets_at":"2026-09-16T18:40:00.595003+00:00"}]})", d) == FetchError::None);

  // ---- base64url dekodolas (RFC 4648 vektorok, url es standard alfabet, padding)
  unsigned char buf[64];
  CHECK(base64urlDecode("Zm9vYmFy", 8, buf, sizeof(buf)) == 6 && std::string((char *)buf) == "foobar");
  CHECK(base64urlDecode("Zm9vYg", 6, buf, sizeof(buf)) == 4 && std::string((char *)buf) == "foob");
  CHECK(base64urlDecode("Zm9vYg==", 8, buf, sizeof(buf)) == 4);
  CHECK(base64urlDecode("-_-_", 4, buf, sizeof(buf)) == 3 && buf[0] == 0xfb && buf[1] == 0xff && buf[2] == 0xbf);
  CHECK(base64urlDecode("+/+/", 4, buf, sizeof(buf)) == 3 && buf[0] == 0xfb);
  CHECK(base64urlDecode("Zm9*", 4, buf, sizeof(buf)) == -1);

  // ---- JWT: a payload {"https://api.openai.com/auth":{"chatgpt_account_id":"acc-123"}} base64url-ben (python-nal kodolva)
  std::string jwt = "eyJhbGciOiJub25lIn0.eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjLTEyMyJ9fQ.sig";
  CHECK(chatgptAccountIdFromIdToken(String(jwt.c_str())) == "acc-123");
  CHECK(chatgptAccountIdFromIdToken(String("nincs-pont")) == "");
  CHECK(chatgptAccountIdFromIdToken(String("a.eyJmb28iOjF9.b")) == "");  // {"foo":1}: nincs claim

  printf("providers fails=%d\n", fails);
  return fails;
}
