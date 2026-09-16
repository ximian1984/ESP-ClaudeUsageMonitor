#include "usage_parser.h"

#include <ArduinoJson.h>

FetchError parseUsage(const String &body, UsageData &out) {
  out = UsageData();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return FetchError::Parse;
  if (!doc.is<JsonObject>()) return FetchError::Parse;

  // ⛔ Ide jon a mezo-lekepezes — CSAK a valos minta alapjan. Addig szandekosan semmi.
  return FetchError::ParserPending;
}
