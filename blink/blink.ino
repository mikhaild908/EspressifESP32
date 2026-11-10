/*
  Inland ESP32 Development Board (USB Type-C) Example
  This program blinks the onboard LED and prints to Serial Monitor.
  Works with ESP-WROOM-32 based boards.
*/

#include <Arduino.h>

// On most ESP32 boards, the onboard LED is on GPIO 2.
// If your board uses a different pin, change LED_PIN accordingly.
#define LED_PIN 2

void setup() {
  // Initialize serial communication at 115200 baud
  Serial.begin(115200);
  // Wait for serial connection (optional)
  delay(1000);

  // Set LED pin as output
  pinMode(LED_PIN, OUTPUT);

  Serial.println("ESP32 Inland Type-C Board Test Starting...");
}

void loop() {
  // Turn LED on
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED ON");
  delay(1000); // Wait 1 second

  // Turn LED off
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED OFF");
  delay(1000); // Wait 1 second
}
