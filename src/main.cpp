// Build-vaz: csak a toolchain + kijelzo-konfig forditasat igazolja. Vason meg NEM futott.
#include <Arduino.h>
#include <TFT_eSPI.h>

static const char *FW_VERSION = "0.0.1-skeleton";
static const int PIN_LCD_BL = 38;  // LilyGO factory_screen.ino:46 — aktiv ALACSONY (ledcWrite 0 = max, lcd.ino:96)

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);
  tft.init();
  tft.setRotation(1);  // 160x80 fekvo — LilyGO examples/TFT_eSPI/TFT_eSPI.ino:35
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Claude Monitor", 4, 4, 2);
  tft.drawString(FW_VERSION, 4, 30, 2);
}

void loop() { delay(10); }
