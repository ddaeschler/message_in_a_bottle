#ifndef APPSERVER_H
#define APPSERVER_H

#include <optional>
#include <WebServer.h>

namespace miab {

    class AppServer {
        public:
            AppServer();
            ~AppServer();

            bool start();
            void tick();

        private:
            void serveIndex();

        private:
            void sendGzippedFile(const std::string& path, 
                const std::string& contentType);

            WebServer _webServer;
    };

}

#endif