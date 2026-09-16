// base64url (RFC 4648 §5, padding nelkul). Kulon fajl, hogy host-tesztelheto legyen (RFC-vektorokkal).
#pragma once
#include <stddef.h>

// Legalabb ((len+2)/3)*4 + 1 bajt kell az out-nak. Visszaadja a beirt karakterek szamat (a lezaro 0 nelkul).
size_t base64urlEncode(const unsigned char *data, size_t len, char *out, size_t outCap);
