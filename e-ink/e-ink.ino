#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// CS, DC, RST, BUSY
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>
display(GxEPD2_213_B74(5, 17, 16, 4));

void setup()
{
  Serial.begin(115200);

  display.init(115200);

  display.setRotation(1);

  display.setFullWindow();

  display.firstPage();

  Serial.println("Hello World!");

  do
  {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(10, 30);
    display.println("Hello there!");

    display.setCursor(10, 55);
    display.println("Hello again!");

  }
  while (display.nextPage());
}

void loop()
{
  // Nothing required
}