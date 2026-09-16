// Host-teszt: base64url (RFC 4648 §5 vektorok) + PKCE-challenge derivalas (RFC 7636 B. vektor).
// A challenge = base64url(SHA-256(verifier)); a SHA-256-ot itt a rendszer-openssl adja, hogy a
// derivalas HELYESSEGET a valos vason futo mbedtls-tol fuggetlenul is igazoljuk.
#include "base64url.h"
#include <cstdio>
#include <cstring>
#include <string>
int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static std::string b64u(const std::string &in){char out[256];base64urlEncode((const unsigned char*)in.data(),in.size(),out,sizeof(out));return out;}
int main(){
  // RFC 4648 §10 (base64) -> url-varians, padding nelkul
  CHECK(b64u("")=="");
  CHECK(b64u("f")=="Zg");
  CHECK(b64u("fo")=="Zm8");
  CHECK(b64u("foo")=="Zm9v");
  CHECK(b64u("foob")=="Zm9vYg");
  CHECK(b64u("fooba")=="Zm9vYmE");
  CHECK(b64u("foobar")=="Zm9vYmFy");
  // url-alfabet: 62/63 -> '-' '_' ; a >>?>>? bajtok -> "Pj4/Pj8=" base64-ban, url-ben "Pj4_Pj8"
  CHECK(b64u(std::string("\x3e\x3e\x3f\x3e\x3f",5))=="Pj4_Pj8");
  CHECK(b64u(std::string("\xfb\xff\xbf",3))=="-_-_");
  // RFC 7636 App. B: verifier "dBjftJ..." -> challenge. A SHA-256 digestet a run.sh openssl-lel szamolja
  // es hex-ben adja at (DIGEST_HEX); itt csak a base64url-derivalast ellenorizzuk az RFC elvart erteke ellen.
#ifdef EXPECT_CHALLENGE
  char out[64]; 
  // a digest a parancssorbol jon hex-ben
  const char *hex=DIGEST_HEX; unsigned char dig[32];
  for(int i=0;i<32;i++){unsigned v;sscanf(hex+2*i,"%2x",&v);dig[i]=(unsigned char)v;}
  base64urlEncode(dig,32,out,sizeof(out));
  CHECK(std::string(out)==EXPECT_CHALLENGE);
#endif
  printf("base64url fails=%d\n",fails);
  return fails;
}
