#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <epd/GxEPD2_213_B74.h>

#define epd_bitmap_allArray_LEN epd_bitmap_lauren_allArray_LEN
#define epd_bitmap_allArray epd_bitmap_lauren_allArray
#include "lauren_bitmap.h"
#undef epd_bitmap_allArray
#undef epd_bitmap_allArray_LEN

#define epd_bitmap_allArray_LEN epd_bitmap_polgas_allArray_LEN
#define epd_bitmap_allArray epd_bitmap_polgas_allArray
#include "polgas_bitmap.h"
#undef epd_bitmap_allArray
#undef epd_bitmap_allArray_LEN

#include <string>

namespace
{

constexpr char BLE_DEVICE_NAME[] = "ESP32 E-Ink Images";
constexpr char SERVICE_UUID[] = "12345678-1234-1234-1234-123456789abc";
constexpr char CHARACTERISTIC_UUID[] = "abcdef01-1234-1234-1234-123456789abc";

constexpr int8_t EPD_SCK_PIN = 18;
constexpr int8_t EPD_MISO_PIN = 19;
constexpr int8_t EPD_MOSI_PIN = 23;
constexpr int8_t EPD_CS_PIN = 5;
constexpr int8_t EPD_DC_PIN = 17;
constexpr int8_t EPD_RST_PIN = 16;
constexpr int8_t EPD_BUSY_PIN = 4;

constexpr uint16_t POLGAS_BITMAP_WIDTH = 320;
constexpr uint16_t POLGAS_BITMAP_HEIGHT = 213;
constexpr uint16_t LAUREN_BITMAP_WIDTH = 427;
constexpr uint16_t LAUREN_BITMAP_HEIGHT = 640;

enum class ImageCommand : uint8_t
{
  None,
  Polgas,
  Lauren
};

GxEPD2_213_B74 epaperDriver(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN,
                            EPD_BUSY_PIN);
GxEPD2_BW<GxEPD2_213_B74, 16> display(epaperDriver);
QueueHandle_t imageCommandQueue = nullptr;

class ImageCommandCallbacks final : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    const String value = characteristic->getValue();
    ImageCommand command = ImageCommand::None;

    if (value == "polgas")
    {
      command = ImageCommand::Polgas;
    }
    else if (value == "lauren")
    {
      command = ImageCommand::Lauren;
    }

    if (command != ImageCommand::None && imageCommandQueue != nullptr)
    {
      xQueueOverwrite(imageCommandQueue, &command);
    }
  }
};

void drawScaledBitmap(const unsigned char *bitmap, uint16_t bitmapWidth,
                      uint16_t bitmapHeight)
{
  const int16_t screenWidth = display.width();
  const int16_t screenHeight = display.height();
  int16_t drawWidth = 0;
  int16_t drawHeight = 0;

  if (static_cast<int32_t>(screenWidth) * bitmapHeight >
      static_cast<int32_t>(screenHeight) * bitmapWidth)
  {
    drawHeight = screenHeight;
    drawWidth = (static_cast<int32_t>(bitmapWidth) * drawHeight +
                 bitmapHeight / 2) / bitmapHeight;
  }
  else
  {
    drawWidth = screenWidth;
    drawHeight = (static_cast<int32_t>(bitmapHeight) * drawWidth +
                  bitmapWidth / 2) / bitmapWidth;
  }

  const int16_t left = (screenWidth - drawWidth) / 2;
  const int16_t top = (screenHeight - drawHeight) / 2;
  const uint16_t rowBytes = (bitmapWidth + 7) / 8;

  for (int16_t y = 0; y < drawHeight; ++y)
  {
    const uint16_t sourceY =
        static_cast<uint32_t>(y) * bitmapHeight / drawHeight;

    for (int16_t x = 0; x < drawWidth; ++x)
    {
      const uint16_t sourceX =
          static_cast<uint32_t>(x) * bitmapWidth / drawWidth;
      const uint32_t byteIndex =
          static_cast<uint32_t>(sourceY) * rowBytes + sourceX / 8;
      const uint8_t packed = pgm_read_byte(bitmap + byteIndex);
      const uint8_t bitMask = 0x80 >> (sourceX & 7);

      // The supplied bitmaps store white as 1 and black as 0.
      if ((packed & bitMask) == 0)
      {
        display.drawPixel(left + x, top + y, GxEPD_BLACK);
      }
    }
  }
}

void displayImage(ImageCommand command)
{
  uint8_t rotation = 0;
  const unsigned char *bitmap = nullptr;
  uint16_t bitmapWidth = 0;
  uint16_t bitmapHeight = 0;

  switch (command)
  {
  case ImageCommand::Polgas:
    rotation = 3;
    bitmap = epd_bitmap_polgas_02;
    bitmapWidth = POLGAS_BITMAP_WIDTH;
    bitmapHeight = POLGAS_BITMAP_HEIGHT;
    Serial.println("Displaying Polgas image.");
    break;
  case ImageCommand::Lauren:
    rotation = 2;
    bitmap = epd_bitmap_lauren_02;
    bitmapWidth = LAUREN_BITMAP_WIDTH;
    bitmapHeight = LAUREN_BITMAP_HEIGHT;
    Serial.println("Displaying Lauren image.");
    break;
  default:
    return;
  }

  display.setRotation(rotation);
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    drawScaledBitmap(bitmap, bitmapWidth, bitmapHeight);
  }
  while (display.nextPage());
}

void startBleServer()
{
  imageCommandQueue = xQueueCreate(1, sizeof(ImageCommand));
  if (imageCommandQueue == nullptr)
  {
    Serial.println("Failed to create BLE image command queue.");
    return;
  }

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
      CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
  characteristic->setCallbacks(new ImageCommandCallbacks());
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  Serial.println("BLE ready. Write 'polgas' or 'lauren' to the image characteristic.");
}

} // namespace

void setup()
{
  Serial.begin(115200);
  SPI.begin(EPD_SCK_PIN, EPD_MISO_PIN, EPD_MOSI_PIN, EPD_CS_PIN);
  display.init(115200);
  display.setRotation(3);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(4, 20);
    display.print("Connect with nRF Connect");
    display.setCursor(4, 36);
    display.print("Write polgas or lauren");
  }
  while (display.nextPage());

  startBleServer();
}

void loop()
{
  ImageCommand command = ImageCommand::None;
  if (imageCommandQueue != nullptr &&
      xQueueReceive(imageCommandQueue, &command, 0) == pdTRUE)
  {
    displayImage(command);
  }
  delay(10);
}
