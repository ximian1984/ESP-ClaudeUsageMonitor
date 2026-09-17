#include "backup_crypto.h"

#include <esp_random.h>
#include <memory>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include "config.h"

static bool deriveKey(const String &pw, const uint8_t *salt, uint32_t iterations, uint8_t *key) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  uint32_t t0 = millis();
  bool ok = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) == 0 &&
            mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const unsigned char *)pw.c_str(), pw.length(), salt, 16, iterations, 32,
                                      key) == 0;
  mbedtls_md_free(&ctx);
  Serial.printf("[backup] PBKDF2 %u kor: %lu ms\n", (unsigned)iterations, (unsigned long)(millis() - t0));
  return ok;
}

static String b64(const uint8_t *data, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);
  std::unique_ptr<unsigned char[]> buf(new unsigned char[olen + 1]);
  if (mbedtls_base64_encode(buf.get(), olen + 1, &olen, data, len) != 0) return String();
  buf[olen] = 0;
  return String((const char *)buf.get());
}

// base64 -> bajtok; false, ha hibas vagy nem a vart hossz (expectLen 0 = barmilyen, legfeljebb maxLen).
static bool unb64(const char *s, std::unique_ptr<uint8_t[]> &out, size_t &outLen, size_t expectLen, size_t maxLen) {
  if (!s) return false;
  size_t slen = strlen(s), need = 0;
  if (slen == 0 || slen > maxLen * 4 / 3 + 8) return false;
  mbedtls_base64_decode(nullptr, 0, &need, (const unsigned char *)s, slen);
  if (need == 0 || need > maxLen) return false;
  out.reset(new uint8_t[need]);
  if (mbedtls_base64_decode(out.get(), need, &outLen, (const unsigned char *)s, slen) != 0) return false;
  return expectLen == 0 || outLen == expectLen;
}

static String aad() { return String(BACKUP_ENC_FORMAT) + "/" + BACKUP_ENC_VERSION; }

bool backupEncrypt(const String &plaintext, const String &password, JsonDocument &env, String &err) {
  uint8_t salt[16], iv[12], tag[16], key[32];
  esp_fill_random(salt, sizeof(salt));
  esp_fill_random(iv, sizeof(iv));
  if (!deriveKey(password, salt, BACKUP_KDF_ITERATIONS, key)) {
    err = "key derivation failed";
    return false;
  }
  size_t n = plaintext.length();
  std::unique_ptr<uint8_t[]> ct(new uint8_t[n ? n : 1]);
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  String a = aad();
  bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
            mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, n, iv, sizeof(iv), (const uint8_t *)a.c_str(), a.length(),
                                      (const uint8_t *)plaintext.c_str(), ct.get(), sizeof(tag), tag) == 0;
  mbedtls_gcm_free(&gcm);
  memset(key, 0, sizeof(key));
  if (!ok) {
    err = "encryption failed";
    return false;
  }
  env["format"] = BACKUP_ENC_FORMAT;
  env["version"] = BACKUP_ENC_VERSION;
  env["kdf"] = "pbkdf2-hmac-sha256";
  env["iterations"] = BACKUP_KDF_ITERATIONS;
  env["salt"] = b64(salt, sizeof(salt));
  env["cipher"] = "aes-256-gcm";
  env["iv"] = b64(iv, sizeof(iv));
  env["tag"] = b64(tag, sizeof(tag));
  env["data"] = b64(ct.get(), n);
  return true;
}

bool backupDecrypt(JsonVariantConst env, const String &password, String &plaintext, String &err) {
  if (!(env["format"] == BACKUP_ENC_FORMAT) || (env["version"] | 0) != BACKUP_ENC_VERSION ||
      !(env["kdf"] == "pbkdf2-hmac-sha256") || !(env["cipher"] == "aes-256-gcm")) {
    err = "unsupported encrypted file (format/version)";
    return false;
  }
  uint32_t iterations = env["iterations"] | (uint32_t)0;
  if (iterations < 1000 || iterations > 2000000) {
    err = "invalid iteration count";
    return false;
  }
  std::unique_ptr<uint8_t[]> salt, iv, tag, ct;
  size_t saltLen, ivLen, tagLen, ctLen;
  if (!unb64(env["salt"], salt, saltLen, 16, 16) || !unb64(env["iv"], iv, ivLen, 12, 12) ||
      !unb64(env["tag"], tag, tagLen, 16, 16) || !unb64(env["data"], ct, ctLen, 0, 16384)) {
    err = "corrupted file (base64 fields)";
    return false;
  }
  uint8_t key[32];
  if (!deriveKey(password, salt.get(), iterations, key)) {
    err = "key derivation failed";
    return false;
  }
  std::unique_ptr<uint8_t[]> pt(new uint8_t[ctLen + 1]);
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  String a = aad();
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (rc == 0)
    rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv.get(), ivLen, (const uint8_t *)a.c_str(), a.length(), tag.get(), tagLen,
                                  ct.get(), pt.get());
  mbedtls_gcm_free(&gcm);
  memset(key, 0, sizeof(key));
  if (rc != 0) {
    err = "wrong password or corrupted file";
    return false;
  }
  pt[ctLen] = 0;
  plaintext = String((const char *)pt.get());
  memset(pt.get(), 0, ctLen);
  return true;
}
