#include <Arduino.h>
#include "CaptiveWifi.h"

miab::CaptiveWifi captiveWifi;

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("Starting Captive Portal WiFi Access Point...");
    captiveWifi.start();
}

void loop() {
    captiveWifi.tick();
}