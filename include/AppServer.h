#ifndef APPSERVER_H
#define APPSERVER_H

#include <WebServer.h>
#include <ArduinoJson.h>
#include "RingBuffer.h"

namespace miab {

    class AppServer {
        public:
            AppServer();
            ~AppServer();

            bool start();
            void tick();

        private:
            void serveIndex();
            void serveRead();
            void serveWrite();
            
        private:
            void sendError(int code, const std::string& text);
            void sendGzippedFile(const std::string& path, 
                const std::string& contentType);
            void sendJson(const JsonDocument& doc);

            WebServer _webServer;
            ring_buffer::RingBuffer _ringBuffer;
    };

}

#endif