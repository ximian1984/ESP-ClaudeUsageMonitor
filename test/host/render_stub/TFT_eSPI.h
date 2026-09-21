// Host-stub: a TFT_eSPI 2.5.43 sprite-rajzolasanak GEOMETRIAJA, a konyvtar SAJAT font-tablaival (Font16, Font32rle,
// Font64rle). Szelesseg = a widtbl osszege (TFT_eSPI.cpp textWidth); datum: TC -> x - w/2, TR -> x - w (drawString);
// a 2-es font bitkep (soronkent (w+6)/8 bajt), a 4/6-os RLE (bit7 = eloter-futas; Extensions/Sprite.cpp drawChar).
// A kilogo pixelt a sprite levagja, mint a vason. A panel (driver, szinsorrend) NINCS benne: csak az elrendezes.
#pragma once
#include <Arduino.h>

#include <vector>
#define PROGMEM
#include <Fonts/Font16.h>
#include <Fonts/Font32rle.h>
#include <Fonts/Font64rle.h>

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
// Szinertekek: TFT_eSPI.h:305-321
#define TFT_BLACK 0x0000
#define TFT_LIGHTGREY 0xD69A
#define TFT_DARKGREY 0x7BEF
#define TFT_GREEN 0x07E0
#define TFT_CYAN 0x07FF
#define TFT_RED 0xF800
#define TFT_YELLOW 0xFFE0
#define TFT_WHITE 0xFFFF
#define TFT_ORANGE 0xFDA0

// A kepernyo merete a forgatastol fugg (setRotation: paratlan = fekvo 320x240, paros = allo 240x320).
extern int g_screenW, g_screenH;
extern std::vector<uint16_t> g_screen;  // a pushSprite ide masol (RGB565, nem bajtcserelt)

class TFT_eSPI {
 public:
  void init() {}
  void setRotation(int r) {
    g_screenW = r & 1 ? 320 : 240;
    g_screenH = r & 1 ? 240 : 320;
  }
  void fillScreen(uint16_t c) { std::fill(g_screen.begin(), g_screen.end(), c); }
};

class TFT_eSprite {
 public:
  explicit TFT_eSprite(TFT_eSPI *) {}
  void *createSprite(int w, int h) {
    _w = w;
    _h = h;
    _buf.assign((size_t)w * h, 0);
    return _buf.data();
  }
  void deleteSprite() { _buf.clear(); }
  void *getPointer() { return _buf.data(); }
  void fillSprite(uint16_t c) { std::fill(_buf.begin(), _buf.end(), c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++) px(i, j, c);
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) {
    fillRect(x, y, w, 1, c);
    fillRect(x, y + h - 1, w, 1, c);
    fillRect(x, y, 1, h, c);
    fillRect(x + w - 1, y, 1, h, c);
  }
  void setTextDatum(uint8_t d) { _datum = d; }
  void setTextColor(uint16_t c) { _fg = c; }
  int16_t fontHeight(uint8_t f) { return f == 2 ? chr_hgt_f16 : f == 4 ? chr_hgt_f32 : chr_hgt_f64; }
  int16_t textWidth(const String &s, uint8_t f) {
    int w = 0;
    for (unsigned char c : s) w += widthOf(c, f);
    return w;
  }
  int16_t drawString(const String &s, int x, int y, uint8_t f) {
    int w = textWidth(s, f);
    if (_datum == TC_DATUM) x -= w / 2;
    else if (_datum == TR_DATUM) x -= w;
    for (unsigned char c : s) x += drawChar(c, x, y, f);
    return w;
  }
  void pushSprite(int x, int y) {
    for (int j = 0; j < _h; j++)
      for (int i = 0; i < _w; i++)
        if (x + i >= 0 && y + j >= 0 && x + i < g_screenW && y + j < g_screenH)
          g_screen[(size_t)(y + j) * g_screenW + x + i] = _buf[(size_t)j * _w + i];
  }

 private:
  static int widthOf(unsigned char c, uint8_t f) {
    if (c < 32 || c > 127) c = 32;  // TFT_eSPI: ervenytelen karakter = szokoz szelessege
    return f == 2 ? widtbl_f16[c - 32] : f == 4 ? widtbl_f32[c - 32] : widtbl_f64[c - 32];
  }
  void px(int x, int y, uint16_t c) {
    if (x >= 0 && y >= 0 && x < _w && y < _h) _buf[(size_t)y * _w + x] = c;
  }
  int drawChar(unsigned char c, int x, int y, uint8_t f) {
    if (c < 32 || c > 127) c = 32;
    int width = widthOf(c, f);
    if (f == 2) {
      const unsigned char *p = chrtbl_f16[c - 32];
      int bpr = (width + 6) / 8;
      for (int r = 0; r < chr_hgt_f16; r++)
        for (int k = 0; k < bpr; k++) {
          unsigned char line = p[r * bpr + k];
          for (int b = 0; b < 8; b++)
            if (line & (0x80 >> b)) px(x + k * 8 + b, y + r, _fg);
        }
      return width;
    }
    const unsigned char *p = f == 4 ? chrtbl_f32[c - 32] : chrtbl_f64[c - 32];
    int total = width * fontHeight(f), pc = 0;
    while (pc < total) {
      unsigned char line = *p++;
      int run = (line & 0x7F) + 1;
      bool fg = line & 0x80;
      for (int i = 0; i < run && pc < total; i++, pc++)
        if (fg) px(x + pc % width, y + pc / width, _fg);
    }
    return width;
  }
  int _w = 0, _h = 0;
  uint8_t _datum = TL_DATUM;
  uint16_t _fg = TFT_WHITE;
  std::vector<uint16_t> _buf;
};
