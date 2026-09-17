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

static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  return -1;
}

long base64urlDecode(const char *in, size_t inLen, unsigned char *out, size_t outCap) {
  unsigned acc = 0;
  int bits = 0;
  size_t o = 0;
  for (size_t i = 0; i < inLen; i++) {
    char c = in[i];
    if (c == '=') break;
    int v = b64val(c);
    if (v < 0) return -1;
    acc = (acc << 6) | (unsigned)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o + 1 >= outCap) return -1;
      out[o++] = (unsigned char)((acc >> bits) & 0xFF);
    }
  }
  if (outCap) out[o] = 0;
  return (long)o;
}
