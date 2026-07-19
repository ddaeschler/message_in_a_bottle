#include "CaptiveWifi.h"
#include <WiFi.h>

namespace miab {
    
    CaptiveWifi::CaptiveWifi() {
        
    }

    bool CaptiveWifi::start() {
        // Initialize WiFi in AP mode
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(ipAddress, ipAddress, IPAddress(255, 255, 255, 0));
        WiFi.softAPsetHostname("message.bottle");
        
        // Start the access point with the specified SSID
        if (!WiFi.softAP("MESSAGE-IN-A-BOTTLE")) {
            Serial.println("Failed to start access point");
            return false;
        }

        // Initialize DNS server. This will resolve everything to the IP address of the ESP32.
        if (!_dnsServer.start(53, "*", ipAddress)) {
            Serial.println("Failed to start DNS server");
            return false;
        }

        return true;
    }

    void CaptiveWifi::tick() {
        _dnsServer.processNextRequest();

    }

    CaptiveWifi::~CaptiveWifi() {
        WiFi.softAPdisconnect(true);
        _dnsServer.stop();
    }

}