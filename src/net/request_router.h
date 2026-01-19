#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "http_server.h"

namespace net {

class RequestRouter {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    // Exact match routes: (METHOD, PATH) -> handler
    void add_route(std::string method, std::string path, Handler handler);

    // If no route matched
    void set_not_found_handler(Handler handler);

    // If path exists but method differs
    void set_method_not_allowed_handler(Handler handler);

    HttpResponse route(const HttpRequest& req) const;

private:
    std::unordered_map<std::string, Handler> routes_;
    std::unordered_map<std::string, bool> known_paths_; // path -> true

    Handler not_found_ = [](const HttpRequest&) {
        return make_json_response(404, R"({"ok":false,"error":"not_found"})");
    };

    Handler method_not_allowed_ = [](const HttpRequest&) {
        return make_json_response(405, R"({"ok":false,"error":"method_not_allowed"})");
    };

    static std::string key(const std::string& method, const std::string& path);
    static std::string upper(std::string s);
};

} // namespace net
