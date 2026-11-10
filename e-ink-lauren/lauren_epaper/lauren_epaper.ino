#include <SPI.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_B74.h>

#include "lauren_bitmap.h"

constexpr int8_t EPD_SCK_PIN = 18;
constexpr int8_t EPD_MISO_PIN = 19; // Not connected to the display.
constexpr int8_t EPD_MOSI_PIN = 23;
constexpr int8_t EPD_CS_PIN = 5;
constexpr int8_t EPD_DC_PIN = 17;
constexpr int8_t EPD_RST_PIN = 16;
constexpr int8_t EPD_BUSY_PIN = 4;

constexpr uint16_t BITMAP_WIDTH = 427;
constexpr uint16_t BITMAP_HEIGHT = 640;
constexpr uint16_t BITMAP_ROW_BYTES = (BITMAP_WIDTH + 7) / 8;

GxEPD2_213_B74 epaperDriver(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN);
GxEPD2_BW<GxEPD2_213_B74, 16> display(epaperDriver);

void drawScaledBitmap()
{
  const int16_t screenWidth = display.width();
  const int16_t screenHeight = display.height();
  int16_t drawWidth;
  int16_t drawHeight;

  if (static_cast<int32_t>(screenWidth) * BITMAP_HEIGHT >
      static_cast<int32_t>(screenHeight) * BITMAP_WIDTH)
  {
    drawHeight = screenHeight;
    drawWidth = (static_cast<int32_t>(BITMAP_WIDTH) * drawHeight +
                 BITMAP_HEIGHT / 2) / BITMAP_HEIGHT;
  }
  else
  {
    drawWidth = screenWidth;
    drawHeight = (static_cast<int32_t>(BITMAP_HEIGHT) * drawWidth +
                  BITMAP_WIDTH / 2) / BITMAP_WIDTH;
  }

  const int16_t left = (screenWidth - drawWidth) / 2;
  const int16_t top = (screenHeight - drawHeight) / 2;

  for (int16_t y = 0; y < drawHeight; ++y)
  {
    const uint16_t sourceY =
        static_cast<uint32_t>(y) * BITMAP_HEIGHT / drawHeight;

    for (int16_t x = 0; x < drawWidth; ++x)
    {
      const uint16_t sourceX =
          static_cast<uint32_t>(x) * BITMAP_WIDTH / drawWidth;
      const uint16_t byteIndex =
          sourceY * BITMAP_ROW_BYTES + sourceX / 8;
      const uint8_t packed = pgm_read_byte(&epd_bitmap_lauren_02[byteIndex]);
      const uint8_t bitMask = 0x80 >> (sourceX & 7);

      // The supplied bitmap stores white as 1 and black as 0.
      if ((packed & bitMask) == 0)
      {
        display.drawPixel(left + x, top + y, GxEPD_BLACK);
      }
    }
  }
}

void setup()
{
  SPI.begin(EPD_SCK_PIN, EPD_MISO_PIN, EPD_MOSI_PIN, EPD_CS_PIN);
  display.init();
  display.setRotation(2);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    drawScaledBitmap();
  }
  while (display.nextPage());

  display.hibernate();
}

void loop()
{
}
