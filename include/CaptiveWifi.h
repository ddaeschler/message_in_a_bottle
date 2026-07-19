#ifndef CAPTIVE_WIFI_H
#define CAPTIVE_WIFI_H

#include <DNSServer.h>
#include <IPAddress.h>

namespace miab {

    /**
     * This class is responsible for setting up a captive portal WiFi access point.
     * It initializes the WiFi in AP mode and configures the network settings.
     * The access point is created with the SSID "MESSAGE-IN-A-BOTTLE" and
     * the IP address is set to 192.168.4.1
     */
    class CaptiveWifi {
    public: 
        const IPAddress ipAddress = IPAddress(192, 168, 4, 1);

        CaptiveWifi();
        ~CaptiveWifi();

        bool start();
        
        void tick();

    private:
        DNSServer _dnsServer;
    };

}

#endif