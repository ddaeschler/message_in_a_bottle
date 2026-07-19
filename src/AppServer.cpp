#include "AppServer.h"

#include <LittleFS.h>

namespace miab {

    AppServer::AppServer() {

    }

    bool AppServer::start() {
        if (! LittleFS.begin()) {
            Serial.println("LittleFS failed to initialize");
            return false;
        }

        _webServer.on("/", HTTP_GET, std::bind(&AppServer::serveIndex, this));
        _webServer.onNotFound(std::bind(&AppServer::serveIndex, this));

        _webServer.begin();

        return true;
    }

    void AppServer::tick() {
        _webServer.handleClient();
    }

    void AppServer::serveIndex() {
        this->sendGzippedFile("/index.html.gz", "text/html");
    }

    void AppServer::sendGzippedFile(const std::string& fileName, 
        const std::string& contentType) {

        auto file = LittleFS.open(fileName.c_str());
        if (!file) {
            _webServer.send(404, "text/plain", "File not found");
            return;
        }

        _webServer.sendHeader("Cache-Control", "public, max-age=3600");
        _webServer.streamFile(file, contentType.c_str());

        file.close();
    }

    AppServer::~AppServer() {

    }

}