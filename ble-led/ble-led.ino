#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Change this pin if needed
#define LED_PIN 2

// Custom BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcdef01-1234-1234-1234-123456789abc"

BLECharacteristic *pCharacteristic;

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();

    if (value.length() > 0) {
      Serial.print("Received: ");
      Serial.println(value);

      if (value == "1") {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED ON");
      }
      else if (value == "0") {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED OFF");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Create BLE device
  BLEDevice::init("ESP32 LED Control");

  // Create BLE Server
  BLEServer *pServer = BLEDevice::createServer();

  // Create BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());

  pCharacteristic->setValue("0");

  pService->start();

  // Start Advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();

  Serial.println("BLE Server Started");
  Serial.println("Device Name: ESP32 LED Control");
}

void loop() {
  delay(100);
}