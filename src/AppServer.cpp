#include "AppServer.h"

#include <LittleFS.h>

#include <charconv>
#include <cstdint>
#include <string_view>

namespace miab {


    AppServer::AppServer() : _ringBuffer(LittleFS) {

    }

    bool AppServer::start() {
        if (! LittleFS.begin()) {
            Serial.println("LittleFS failed to initialize");
            return false;
        }

        File root = LittleFS.open("/", "r");
        File file = root.openNextFile();

        Serial.println("Files on Flash:");
        while (file) {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
            file.close();
            file = root.openNextFile();
        }

        root.close();
        
        auto err = _ringBuffer.open();
        if (err != ring_buffer::Error::none) {
            Serial.println("Unable to open ring buffer");
            Serial.println(ring_buffer::errorMessage(err));
            return false;
        }

        _webServer.on("/", HTTP_GET, std::bind(&AppServer::serveIndex, this));

        // API Endpoints
        _webServer.on("/read", HTTP_GET, std::bind(&AppServer::serveRead, this));
        _webServer.on("/write", HTTP_POST, std::bind(&AppServer::serveWrite, this));

        _webServer.onNotFound([this] {
            _webServer.sendHeader(
                "Location",
                "http://message.bottle/",
                true
            );

            _webServer.send(
                302,
                "text/plain",
                ""
            );
        });

        


        _webServer.begin();

        return true;
    }

    void AppServer::tick() {
        _webServer.handleClient();
    }

    void AppServer::serveIndex() {
        Serial.printf(
            "HTTP %s %s from %s\n",
            _webServer.method() == HTTP_GET ? "GET" : "OTHER",
            _webServer.uri().c_str(),
            _webServer.client().remoteIP().toString().c_str()
        );

        Serial.printf("Host: %s\n", _webServer.hostHeader().c_str());

        for (int i = 0; i < _webServer.args(); ++i) {
            Serial.printf(
                "Arg %s = %s\n",
                _webServer.argName(i).c_str(),
                _webServer.arg(i).c_str()
            );
        }

        this->sendGzippedFile("/index.html.gz", "text/html");
    }

    bool parseMessageId(std::string_view text, std::uint64_t& value) {
        if (text.empty()) {
            return false;
        }

        const char* begin = text.data();
        const char* end = begin + text.size();

        const auto [ptr, error] =
            std::from_chars(begin, end, value, 10);

        return error == std::errc{} && ptr == end;
    }

    void AppServer::sendError(int code, const std::string& text) {
        _webServer.send(code, "text/plain", text.c_str());
    }

    void AppServer::serveRead() {
        if (_webServer.args() > 1) {
            sendError(400, "Too many arguments");
            return;
        }

        if (_webServer.args() == 1 && _webServer.argName(0) != "afterId") {
            sendError(400, "Invalid argument");
            return;
        }

        std::uint64_t afterId = 0;
        if (_webServer.args() == 1) {
            const String rawAfterId = _webServer.arg("afterId");
            if (!parseMessageId(
                std::string_view{
                    rawAfterId.c_str(),
                    rawAfterId.length()
                }, afterId)) {

                    sendError(400, "Invalid afterId parameter");
                    return;
                }
        }

        std::vector<ring_buffer::Entry> entries;
        if (afterId == 0) {
            const auto error = _ringBuffer.readAll(entries);
            if (error != ring_buffer::Error::none) {
                sendError(500, "Unable to read ring buffer");
                return;
            }
        } else {
            const auto error = _ringBuffer.readAfter(afterId, entries);
            if (error != ring_buffer::Error::none) {
                sendError(500, "Unable to read ring buffer");
                return;
            }
        }

        JsonDocument ret;
        JsonArray jsonEntries = ret["entries"].to<JsonArray>();

        for (const auto& entry : entries) {
            JsonObject jsonEntry = jsonEntries.add<JsonObject>();

            jsonEntry["id"] = entry.id;
            jsonEntry["handle"] = entry.handle;
            jsonEntry["message"] = entry.message;
        }

        Serial.printf(
            "Sending %zu entries to Host: %s\n",
            entries.size(),
            _webServer.hostHeader().c_str()
        );

        sendJson(ret);
    }

    void AppServer::serveWrite()
    {
        if (!_webServer.hasArg("plain")) {
            _webServer.send(
                400,
                "application/json",
                R"({"error":"missing request body"})"
            );
            return;
        }

        const String body = _webServer.arg("plain");

        JsonDocument doc;
        const auto error = deserializeJson(doc, body);

        if (error) {
            _webServer.send(
                400,
                "application/json",
                R"({"error":"invalid JSON"})"
            );
            return;
        }

        const char* handle = doc["handle"];
        const char* message = doc["message"];

        if (handle == nullptr || message == nullptr) {
            _webServer.send(
                400,
                "application/json",
                R"({"error":"handle and message are required"})"
            );
            return;
        }

        Serial.printf("handle=%s\n", handle);
        Serial.printf("message=%s\n", message);

        const auto result = _ringBuffer.writeNext(handle, message);

        if (!result) {
            _webServer.send(
                500,
                "application/json",
                R"({"error":"failed to store message"})"
            );
            return;
        }

        JsonDocument response;
        JsonObject entry = response["entry"].to<JsonObject>();

        entry["id"] = result.entry.id;
        entry["handle"] = result.entry.handle;
        entry["message"] = result.entry.message;

        sendJson(response);
    }

    void AppServer::sendJson(const JsonDocument& doc) {
        String responseBody;
        serializeJson(doc, responseBody);

        _webServer.send(200, "application/json", responseBody);
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