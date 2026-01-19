#include "http_server.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

namespace net {

static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string trim(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < in.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
                if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
                return -1;
            };
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static void parse_query(const std::string& qs, std::unordered_map<std::string, std::string>& out) {
    size_t start = 0;
    while (start < qs.size()) {
        size_t amp = qs.find('&', start);
        std::string part = (amp == std::string::npos) ? qs.substr(start) : qs.substr(start, amp - start);
        size_t eq = part.find('=');
        if (eq == std::string::npos) {
            auto k = url_decode(part);
            if (!k.empty()) out[k] = "";
        } else {
            auto k = url_decode(part.substr(0, eq));
            auto v = url_decode(part.substr(eq + 1));
            if (!k.empty()) out[k] = v;
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
}

static std::optional<long long> header_content_length(const std::unordered_map<std::string, std::string>& headers) {
    auto it = headers.find("content-length");
    if (it == headers.end()) return std::nullopt;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return std::nullopt;
    }
}

static std::string status_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

static std::string build_response_bytes(const HttpResponse& resp) {
    std::ostringstream oss;
    int st = resp.status;
    std::string reason = resp.reason.empty() ? status_reason(st) : resp.reason;

    oss << "HTTP/1.1 " << st << " " << reason << "\r\n";

    // Ensure Connection close to simplify server logic
    bool has_conn = false;
    bool has_len = false;

    for (const auto& [k, v] : resp.headers) {
        std::string lk = to_lower(k);
        if (lk == "connection") has_conn = true;
        if (lk == "content-length") has_len = true;
        oss << k << ": " << v << "\r\n";
    }

    if (!has_conn) {
        oss << "Connection: close\r\n";
    }
    if (!has_len) {
        oss << "Content-Length: " << resp.body.size() << "\r\n";
    }

    oss << "\r\n";
    oss << resp.body;
    return oss.str();
}

#ifdef _WIN32
static bool wsa_inited = false;
static void ensure_wsa() {
    if (wsa_inited) return;
    WSADATA wsaData;
    int r = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (r != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
    wsa_inited = true;
}
static void closesock(int fd) { closesocket((SOCKET)fd); }
#else
static void closesock(int fd) { ::close(fd); }
#endif

static bool send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = ::send((SOCKET)fd, data + sent, (int)(len - sent), 0);
#else
        ssize_t n = ::send(fd, data + sent, len - sent, 0);
#endif
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static std::optional<std::string> recv_some(int fd) {
    std::vector<char> buf(8192);
#ifdef _WIN32
    int n = ::recv((SOCKET)fd, buf.data(), (int)buf.size(), 0);
#else
    ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
#endif
    if (n <= 0) return std::nullopt;
    return std::string(buf.data(), buf.data() + n);
}

static std::optional<HttpRequest> parse_http_request(const std::string& raw, std::string* err) {
    // split headers and body
    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (err) *err = "No header terminator";
        return std::nullopt;
    }

    std::string header_block = raw.substr(0, header_end);
    std::string body = raw.substr(header_end + 4);

    std::istringstream iss(header_block);
    std::string request_line;
    if (!std::getline(iss, request_line)) {
        if (err) *err = "Empty request";
        return std::nullopt;
    }
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();

    std::istringstream rls(request_line);
    HttpRequest req;
    if (!(rls >> req.method >> req.target >> req.http_version)) {
        if (err) *err = "Bad request line";
        return std::nullopt;
    }

    // headers
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        if (!key.empty()) req.headers[to_lower(key)] = val;
    }

    // path + query
    size_t qpos = req.target.find('?');
    if (qpos == std::string::npos) {
        req.path = req.target;
    } else {
        req.path = req.target.substr(0, qpos);
        parse_query(req.target.substr(qpos + 1), req.query);
    }

    req.body = std::move(body);
    return req;
}

HttpServer::HttpServer(std::string host, uint16_t port, Handler handler)
    : host_(std::move(host)), port_(port), handler_(std::move(handler)) {}

void HttpServer::close_listen_socket_() {
    if (listen_fd_ != -1) {
        closesock(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::stop() {
    stopping_ = true;
    close_listen_socket_(); // this should unblock accept
}

void HttpServer::run() {
#ifdef _WIN32
    ensure_wsa();
#endif

    stopping_ = false;

    // Create socket
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("socket() failed");
    listen_fd_ = (int)s;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) throw std::runtime_error("socket() failed");
    listen_fd_ = s;
#endif

    // Reuse address
    int opt = 1;
#ifdef _WIN32
    ::setsockopt((SOCKET)listen_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (host_.empty() || host_ == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
#ifdef _WIN32
        if (InetPtonA(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
            close_listen_socket_();
            throw std::runtime_error("Invalid bind address");
        }
#else
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
            close_listen_socket_();
            throw std::runtime_error("Invalid bind address");
        }
#endif
    }

#ifdef _WIN32
    if (::bind((SOCKET)listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close_listen_socket_();
        throw std::runtime_error("bind() failed");
    }
    if (::listen((SOCKET)listen_fd_, 128) != 0) {
        close_listen_socket_();
        throw std::runtime_error("listen() failed");
    }
#else
    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close_listen_socket_();
        throw std::runtime_error("bind() failed");
    }
    if (::listen(listen_fd_, 128) != 0) {
        close_listen_socket_();
        throw std::runtime_error("listen() failed");
    }
#endif

    std::cout << "[HttpServer] Listening on " << (host_.empty() ? "0.0.0.0" : host_) << ":" << port_ << "\n";

    while (!stopping_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int clen = sizeof(client_addr);
        SOCKET c = ::accept((SOCKET)listen_fd_, (sockaddr*)&client_addr, &clen);
        if (c == INVALID_SOCKET) {
            if (stopping_) break;
            continue;
        }
        int client_fd = (int)c;
#else
        socklen_t clen = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_, (sockaddr*)&client_addr, &clen);
        if (client_fd < 0) {
            if (stopping_) break;
            continue;
        }
#endif

        char ipbuf[64] = {0};
#ifdef _WIN32
        InetNtopA(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
#else
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
#endif
        std::string remote = std::string(ipbuf) + ":" + std::to_string(ntohs(client_addr.sin_port));

        // Very simple concurrency model: 1 thread per connection.
        // OK for coursework + clear reasoning. You can later swap to your ThreadPool if desired.
        std::thread([this, client_fd, remote]() {
            // Read request (headers + optional body)
            const size_t MAX_HEADER = 1024 * 1024;  // 1MB
            const size_t MAX_BODY   = 10 * 1024 * 1024; // 10MB

            std::string raw;
            raw.reserve(8192);

            // read until we have headers
            while (true) {
                auto chunk = recv_some(client_fd);
                if (!chunk.has_value()) break;
                raw += *chunk;
                if (raw.size() > MAX_HEADER) break;
                if (raw.find("\r\n\r\n") != std::string::npos) break;
            }

            HttpResponse resp;
            resp.status = 400;
            resp.reason = status_reason(400);
            resp.headers["Content-Type"] = "text/plain; charset=utf-8";
            resp.body = "Bad Request";

            std::string parse_err;
            auto req_opt = parse_http_request(raw, &parse_err);
            if (!req_opt.has_value()) {
                resp.body = "Bad Request: " + parse_err;
                std::string bytes = build_response_bytes(resp);
                send_all(client_fd, bytes.data(), bytes.size());
                closesock(client_fd);
                return;
            }

            HttpRequest req = std::move(*req_opt);
            req.remote_addr = remote;

            // If there is Content-Length and body not fully read, read the rest
            auto cl_opt = header_content_length(req.headers);
            if (cl_opt.has_value()) {
                long long need = *cl_opt;
                if (need < 0 || (size_t)need > MAX_BODY) {
                    HttpResponse r413;
                    r413.status = 413;
                    r413.reason = status_reason(413);
                    r413.headers["Content-Type"] = "text/plain; charset=utf-8";
                    r413.body = "Payload Too Large";
                    std::string bytes = build_response_bytes(r413);
                    send_all(client_fd, bytes.data(), bytes.size());
                    closesock(client_fd);
                    return;
                }

                // parse_http_request took whatever was after header_end; might be partial
                while ((long long)req.body.size() < need) {
                    auto chunk = recv_some(client_fd);
                    if (!chunk.has_value()) break;
                    req.body += *chunk;
                }

                if ((long long)req.body.size() > need) {
                    req.body.resize((size_t)need); // ignore extra
                }
            }

            // Handle request
            try {
                HttpResponse out = handler_(req);
                if (out.reason.empty()) out.reason = status_reason(out.status);
                std::string bytes = build_response_bytes(out);
                send_all(client_fd, bytes.data(), bytes.size());
            } catch (const std::exception& e) {
                HttpResponse r500;
                r500.status = 500;
                r500.reason = status_reason(500);
                r500.headers["Content-Type"] = "text/plain; charset=utf-8";
                r500.body = std::string("Internal Server Error: ") + e.what();
                std::string bytes = build_response_bytes(r500);
                send_all(client_fd, bytes.data(), bytes.size());
            } catch (...) {
                HttpResponse r500;
                r500.status = 500;
                r500.reason = status_reason(500);
                r500.headers["Content-Type"] = "text/plain; charset=utf-8";
                r500.body = "Internal Server Error";
                std::string bytes = build_response_bytes(r500);
                send_all(client_fd, bytes.data(), bytes.size());
            }

            closesock(client_fd);
        }).detach();
    }

    close_listen_socket_();
}

HttpResponse make_text_response(int status, const std::string& text) {
    HttpResponse r;
    r.status = status;
    r.reason = status_reason(status);
    r.headers["Content-Type"] = "text/plain; charset=utf-8";
    r.body = text;
    return r;
}

HttpResponse make_json_response(int status, const std::string& json) {
    HttpResponse r;
    r.status = status;
    r.reason = status_reason(status);
    r.headers["Content-Type"] = "application/json; charset=utf-8";
    r.body = json;
    return r;
}

} // namespace net
