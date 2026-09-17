// Titkositott beallitas-mentes (projektgazda, 2026-09-17): a titkot tartalmazo exportot az admin-jelszoval titkositjuk.
// A dongle-on fut, mert a bongeszo WebCrypto API-ja sima HTTP-n (nem "secure context") nem erheto el.
//   Kulcs:     PBKDF2-HMAC-SHA256(jelszo, 16 bajt veletlen so, BACKUP_KDF_ITERATIONS) -> 32 bajt
//   Titkositas: AES-256-GCM, 12 bajt veletlen IV, 16 bajt tag, AAD = BACKUP_ENC_FORMAT "/" verzio
// Boritek (JSON, minden bajtsor base64): format, version, kdf, iterations, salt, cipher, iv, tag, data.
// Szabvanyos primitivek: a fajl a dongle nelkul is visszafejtheto (tools/decrypt_backup.py).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

static const char *const BACKUP_ENC_FORMAT = "device-config-encrypted";
static const int BACKUP_ENC_VERSION = 1;

// plaintext -> boritek. false + err, ha a kulcsszarmaztatas/titkositas nem sikerult.
bool backupEncrypt(const String &plaintext, const String &password, JsonDocument &envelope, String &err);

// boritek -> plaintext. false + err; rossz jelszo vagy serult fajl: err = "wrong password or corrupted file".
bool backupDecrypt(JsonVariantConst envelope, const String &password, String &plaintext, String &err);
