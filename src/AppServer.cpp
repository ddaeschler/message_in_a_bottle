#include "AppServer.h"

namespace miab {

    AppServer::AppServer() {

    }

    bool AppServer::start() {
        _webServer.on("/", HTTP_GET, [this] {
            _webServer.send(
                200,
                "text/plain",
                "Message bottle is alive\n"
            );
        });

        _webServer.onNotFound([this] {
            _webServer.send(
                200,
                "text/html",
                "Message bottle is alive\n"
            );
        });

        _webServer.begin();

        return true;
    }

    void AppServer::tick() {
        _webServer.handleClient();
    }

    AppServer::~AppServer() {

    }

}