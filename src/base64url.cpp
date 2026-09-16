#include "base64url.h"

static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t base64urlEncode(const unsigned char *data, size_t len, char *out, size_t outCap) {
  size_t o = 0;
  size_t i = 0;
  while (i + 3 <= len) {
    unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    if (o + 4 >= outCap) break;
    out[o++] = ALPHABET[(v >> 18) & 0x3F];
    out[o++] = ALPHABET[(v >> 12) & 0x3F];
    out[o++] = ALPHABET[(v >> 6) & 0x3F];
    out[o++] = ALPHABET[v & 0x3F];
    i += 3;
  }
  size_t rem = len - i;
  if (rem == 1 && o + 2 < outCap) {
    unsigned v = data[i] << 16;
    out[o++] = ALPHABET[(v >> 18) & 0x3F];
    out[o++] = ALPHABET[(v >> 12) & 0x3F];
  } else if (rem == 2 && o + 3 < outCap) {
    unsigned v = (data[i] << 16) | (data[i + 1] << 8);
    out[o++] = ALPHABET[(v >> 18) & 0x3F];
    out[o++] = ALPHABET[(v >> 12) & 0x3F];
    out[o++] = ALPHABET[(v >> 6) & 0x3F];
  }
  if (o < outCap) out[o] = '\0';
  return o;
}
