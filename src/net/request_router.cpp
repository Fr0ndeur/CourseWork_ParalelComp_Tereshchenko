#include "request_router.h"

#include <algorithm>
#include <cctype>

namespace net {

std::string RequestRouter::upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string RequestRouter::key(const std::string& method, const std::string& path) {
    return upper(method) + " " + path;
}

void RequestRouter::add_route(std::string method, std::string path, Handler handler) {
    routes_[key(method, path)] = std::move(handler);
    known_paths_[path] = true;
}

void RequestRouter::set_not_found_handler(Handler handler) {
    not_found_ = std::move(handler);
}

void RequestRouter::set_method_not_allowed_handler(Handler handler) {
    method_not_allowed_ = std::move(handler);
}

HttpResponse RequestRouter::route(const HttpRequest& req) const {
    auto it = routes_.find(key(req.method, req.path));
    if (it != routes_.end()) {
        return it->second(req);
    }

    // If path exists but method differs => 405
    if (known_paths_.find(req.path) != known_paths_.end()) {
        return method_not_allowed_(req);
    }

    return not_found_(req);
}

} // namespace net
