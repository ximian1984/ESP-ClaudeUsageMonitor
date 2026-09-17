#include "jwt_util.h"

#include <ArduinoJson.h>

#include <memory>

#include "base64url.h"

String jwtPayloadJson(const String &jwt) {
  int a = jwt.indexOf('.');
  if (a < 0) return String();
  int b = jwt.indexOf('.', a + 1);
  if (b < 0) return String();
  size_t n = (size_t)(b - a - 1);
  if (n == 0 || n > 8192) return String();
  std::unique_ptr<unsigned char[]> buf(new unsigned char[n + 4]);
  long len = base64urlDecode(jwt.c_str() + a + 1, n, buf.get(), n + 4);
  if (len <= 0) return String();
  return String((const char *)buf.get());
}

String chatgptAccountIdFromIdToken(const String &idToken) {
  String payload = jwtPayloadJson(idToken);
  if (payload.length() == 0) return String();
  JsonDocument doc;
  if (deserializeJson(doc, payload.c_str())) return String();
  const char *id = doc["https://api.openai.com/auth"]["chatgpt_account_id"] | "";
  return String(id);
}
