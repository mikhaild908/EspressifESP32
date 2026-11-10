#include "WiFi.h"
void setup() {
 Serial.begin(115200);
 WiFi.mode(WIFI_STA);
 WiFi.disconnect();
 delay(100);
 Serial.println("Scanning WiFi...");
 int n = WiFi.scanNetworks();
 for (int i = 0; i < n; ++i) {
   Serial.printf("%d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
 }
}
void loop() {
 // No loop actions
}