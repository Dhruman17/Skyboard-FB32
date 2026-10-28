#ifndef GATEWAY_DISPLAY_H
#define GATEWAY_DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// C6 gateway status LCD pinout from the proven colleague gateway design.
namespace GatewayDisplay {

constexpr int kCs = 18;
constexpr int kDc = 11;
constexpr int kRst = 1;
constexpr int kSck = 6;
constexpr int kMosi = 7;
constexpr int kBacklight = 10;

Adafruit_ST7735 tft(kCs, kDc, kRst);

inline void begin() {
  pinMode(kBacklight, OUTPUT);
  digitalWrite(kBacklight, HIGH);
  SPI.begin(kSck, -1, kMosi, kCs);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setCursor(8, 8);
  tft.println("SkyAcres Gateway");
}

inline void update(float tempC, float humidity, uint16_t co2, int paired,
                   bool pairingMode) {
  tft.fillRect(0, 22, 160, 106, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  tft.setCursor(8, 28);
  tft.print("Temp: ");
  tft.print(tempC, 1);
  tft.println(" C");

  tft.setCursor(8, 52);
  tft.print("RH:   ");
  tft.print(humidity, 1);
  tft.println(" %");

  tft.setCursor(8, 76);
  tft.print("CO2:  ");
  tft.print(co2);
  tft.println(" ppm");

  tft.setCursor(8, 100);
  tft.print("Nodes: ");
  tft.println(paired);

  tft.setCursor(8, 116);
  tft.setTextColor(pairingMode ? ST77XX_YELLOW : ST77XX_GREEN);
  tft.println(pairingMode ? "PAIRING" : "ONLINE");
}

}  // namespace GatewayDisplay

#endif  // GATEWAY_DISPLAY_H
