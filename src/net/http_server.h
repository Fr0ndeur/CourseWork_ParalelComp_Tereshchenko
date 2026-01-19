#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace net {

struct HttpRequest {
    std::string method;                 // "GET", "POST"
    std::string target;                 // "/search?q=abc"
    std::string path;                   // "/search"
    std::unordered_map<std::string, std::string> query;    // {"q":"abc"}
    std::string http_version;           // "HTTP/1.1"
    std::unordered_map<std::string, std::string> headers;  // lowercased keys
    std::string body;

    // Best-effort, may be empty.
    std::string remote_addr;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(std::string host, uint16_t port, Handler handler);
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Blocking run loop (accept -> spawn handler thread per connection)
    void run();

    // Ask server to stop (unblocks accept loop by closing listen socket)
    void stop();

    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }

private:
    std::string host_;
    uint16_t port_;
    Handler handler_;

    // platform socket handle stored as int-like
    int listen_fd_ = -1;
    bool stopping_ = false;

    void close_listen_socket_();
};

/// Helpers (optional to use from app)
HttpResponse make_text_response(int status, const std::string& text);
HttpResponse make_json_response(int status, const std::string& json);

} // namespace net
