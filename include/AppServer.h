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
            WebServer _webServer;
    };

}

#endif