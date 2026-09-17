// Opcionalis admin-jelszo a setup-oldal MODOSITO kereseihez.
// - Alapertelmezetten nincs jelszo (elso beallitas nyitott).
// - NVS-ben csak so + PBKDF2-HMAC-SHA256 kivonat van, a jelszo nem.
// - Belepes utan veletlen, memoriaban tartott token (ujrainditaskor elvesz).
// - Forced setup modban (BOOT gomb = fizikai hozzaferes) nem kell jelszo: ez a helyreallitasi ut.
#pragma once
#include <Arduino.h>

class AdminAuth {
 public:
  void begin();
  bool passwordSet() const { return _set; }

  enum class LoginResult : uint8_t { Ok, Wrong, LockedOut };
  LoginResult login(const String &password, String &tokenOut);
  // touch=true: a lejarat csuszik (felhasznaloi muvelet). Az 5 s-os status-poll touch=false-szal hiv, kulonben a nyitva
  // hagyott oldal soha nem jarna le.
  bool tokenValid(const String &token, bool touch = true);
  void logoutAll();

  // Ures jelszo = torles. Hibas hossz -> false. Minden token ervenytelenul.
  bool setPassword(const String &password);

 private:
  bool verify(const String &password);
  bool _set = false;
  uint8_t _salt[16] = {0};
  uint8_t _hash[32] = {0};
  uint8_t _failures = 0;
  uint32_t _lockUntilMs = 0;
  SemaphoreHandle_t _mtx = nullptr;
};

extern AdminAuth adminAuth;

static const size_t ADMIN_PASS_MIN = 8;
static const size_t ADMIN_PASS_MAX = 64;
