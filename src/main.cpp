#include <Arduino.h>
#include "CaptiveWifi.h"
#include "AppServer.h"

miab::CaptiveWifi captiveWifi;
miab::AppServer appServer;

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("Starting Captive Portal WiFi Access Point...");
    if (!captiveWifi.start()) {
      return;
    }

    Serial.println("Starting webserver...");
    if (!appServer.start()) {
      return;
    }
}

void loop() {
    captiveWifi.tick();
    appServer.tick();
}