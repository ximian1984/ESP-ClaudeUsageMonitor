#include "admin_auth.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

AdminAuth adminAuth;

static const char *NS = "cmon_adm";
// ⚠ [vason merendo] az iteracioszam futasideje. Alacsonyabb a szokasos szerveroldali ertekeknel,
// mert a webszerver-kezeloben fut; NVS-kiolvasas elleni vedelemnek szant, nem eros offline-tores ellen.
static const unsigned PBKDF2_ITERATIONS = 4096;
static const int MAX_TOKENS = 4;
static const uint32_t TOKEN_TTL_MS = 30UL * 60UL * 1000UL;
static const uint8_t MAX_FAILURES = 5;
static const uint32_t LOCKOUT_MS = 60000;

struct Token {
  char value[33] = "";
  uint32_t lastUseMs = 0;
};
static Token tokens[MAX_TOKENS];

static bool derive(const String &pw, const uint8_t *salt, uint8_t *out) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  bool ok = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) == 0 &&
            mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const unsigned char *)pw.c_str(), pw.length(), salt, 16, PBKDF2_ITERATIONS, 32,
                                      out) == 0;
  mbedtls_md_free(&ctx);
  return ok;
}

static bool constTimeEq(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

void AdminAuth::begin() {
  _mtx = xSemaphoreCreateMutex();
  Preferences p;
  p.begin(NS, true);
  _set = p.getBytesLength("hash") == sizeof(_hash) && p.getBytesLength("salt") == sizeof(_salt);
  if (_set) {
    p.getBytes("salt", _salt, sizeof(_salt));
    p.getBytes("hash", _hash, sizeof(_hash));
  }
  p.end();
}

bool AdminAuth::verify(const String &password) {
  uint8_t h[32];
  if (!derive(password, _salt, h)) return false;
  return constTimeEq(h, _hash, sizeof(h));
}

AdminAuth::LoginResult AdminAuth::checkPassword(const String &password) {
  String unused;
  return login(password, unused, false);
}

AdminAuth::LoginResult AdminAuth::login(const String &password, String &tokenOut) { return login(password, tokenOut, true); }

AdminAuth::LoginResult AdminAuth::login(const String &password, String &tokenOut, bool issueToken) {
  xSemaphoreTake(_mtx, portMAX_DELAY);
  LoginResult r = LoginResult::Wrong;
  uint32_t now = millis();
  if (_lockUntilMs && (int32_t)(now - _lockUntilMs) < 0) {
    r = LoginResult::LockedOut;
  } else if (_set && verify(password) && !issueToken) {
    _failures = 0;
    _lockUntilMs = 0;
    r = LoginResult::Ok;
  } else if (_set && issueToken && verify(password)) {
    _failures = 0;
    _lockUntilMs = 0;
    // Uj token: a legregebbi helyet irja felul.
    int slot = 0;
    for (int i = 1; i < MAX_TOKENS; i++)
      if (tokens[i].lastUseMs < tokens[slot].lastUseMs) slot = i;
    for (int i = 0; i < 32; i++) tokens[slot].value[i] = "0123456789abcdef"[esp_random() & 0x0F];
    tokens[slot].value[32] = '\0';
    tokens[slot].lastUseMs = now | 1;  // 0 = ures hely
    tokenOut = tokens[slot].value;
    r = LoginResult::Ok;
  } else {
    _lockUntilMs = 0;
    if (++_failures >= MAX_FAILURES) {
      _failures = 0;
      _lockUntilMs = now + LOCKOUT_MS;
    }
  }
  xSemaphoreGive(_mtx);
  return r;
}

bool AdminAuth::tokenValid(const String &token, bool touch) {
  if (token.length() != 32) return false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  bool ok = false;
  uint32_t now = millis();
  for (Token &t : tokens) {
    if (t.lastUseMs == 0) continue;
    if (now - t.lastUseMs > TOKEN_TTL_MS) {
      t = Token();
      continue;
    }
    if (constTimeEq((const uint8_t *)t.value, (const uint8_t *)token.c_str(), 32)) {
      if (touch) t.lastUseMs = now | 1;
      ok = true;
    }
  }
  xSemaphoreGive(_mtx);
  return ok;
}

void AdminAuth::logoutAll() {
  xSemaphoreTake(_mtx, portMAX_DELAY);
  for (Token &t : tokens) t = Token();
  xSemaphoreGive(_mtx);
}

bool AdminAuth::setPassword(const String &password) {
  if (!password.isEmpty() && (password.length() < ADMIN_PASS_MIN || password.length() > ADMIN_PASS_MAX)) return false;
  uint8_t salt[16], hash[32];
  if (!password.isEmpty()) {
    esp_fill_random(salt, sizeof(salt));
    if (!derive(password, salt, hash)) return false;
  }
  xSemaphoreTake(_mtx, portMAX_DELAY);
  Preferences p;
  p.begin(NS, false);
  if (password.isEmpty()) {
    p.remove("salt");
    p.remove("hash");
    _set = false;
  } else {
    p.putBytes("salt", salt, sizeof(salt));
    p.putBytes("hash", hash, sizeof(hash));
    memcpy(_salt, salt, sizeof(salt));
    memcpy(_hash, hash, sizeof(hash));
    _set = true;
  }
  p.end();
  for (Token &t : tokens) t = Token();
  xSemaphoreGive(_mtx);
  return true;
}
