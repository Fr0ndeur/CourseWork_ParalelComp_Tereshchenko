#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace {

#ifdef _WIN32
bool wsa_inited = false;
void ensure_wsa() {
    if (wsa_inited) return;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
    wsa_inited = true;
}
static void closesock(int fd) { closesocket((SOCKET)fd); }
#else
static void closesock(int fd) { ::close(fd); }
#endif

struct HttpResp {
    int status = 0;
    std::string reason;
    std::string headers;
    std::string body;
};

std::optional<int> connect_tcp(const std::string& host, int port) {
#ifdef _WIN32
    ensure_wsa();
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    std::string port_s = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) {
        return std::nullopt;
    }

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
#ifdef _WIN32
        SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) {
            fd = (int)s;
            break;
        }
        closesocket(s);
#else
        int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        if (connect(s, p->ai_addr, p->ai_addrlen) == 0) {
            fd = s;
            break;
        }
        ::close(s);
#endif
    }

    freeaddrinfo(res);
    if (fd == -1) return std::nullopt;
    return fd;
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        int n = send((SOCKET)fd, data.data() + sent, (int)(data.size() - sent), 0);
#else
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

std::string recv_all(int fd) {
    std::string out;
    std::vector<char> buf(8192);
    while (true) {
#ifdef _WIN32
        int n = recv((SOCKET)fd, buf.data(), (int)buf.size(), 0);
#else
        ssize_t n = recv(fd, buf.data(), buf.size(), 0);
#endif
        if (n <= 0) break;
        out.append(buf.data(), buf.data() + n);
    }
    return out;
}

HttpResp parse_http_response(const std::string& raw) {
    HttpResp r;
    auto p = raw.find("\r\n\r\n");
    if (p == std::string::npos) return r;
    std::string head = raw.substr(0, p);
    r.body = raw.substr(p + 4);

    std::istringstream iss(head);
    std::string status_line;
    std::getline(iss, status_line);
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();

    std::istringstream sl(status_line);
    std::string httpver;
    sl >> httpver >> r.status;
    std::getline(sl, r.reason);
    if (!r.reason.empty() && r.reason[0] == ' ') r.reason.erase(r.reason.begin());

    r.headers = head;
    return r;
}

std::string url_encode(std::string s) {
    auto hex = [](unsigned char c) {
        const char* d = "0123456789ABCDEF";
        std::string o;
        o.push_back(d[(c >> 4) & 0xF]);
        o.push_back(d[c & 0xF]);
        return o;
    };

    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back((char)c);
        else if (c == ' ') out.push_back('+');
        else { out.push_back('%'); out += hex(c); }
    }
    return out;
}

HttpResp http_get(const std::string& host, int port, const std::string& path) {
    auto fd_opt = connect_tcp(host, port);
    if (!fd_opt.has_value()) throw std::runtime_error("connect failed");
    int fd = *fd_opt;

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Connection: close\r\n"
        << "\r\n";

    if (!send_all(fd, req.str())) {
        closesock(fd);
        throw std::runtime_error("send failed");
    }

    std::string raw = recv_all(fd);
    closesock(fd);
    return parse_http_response(raw);
}

HttpResp http_post_json(const std::string& host, int port, const std::string& path, const std::string& json) {
    auto fd_opt = connect_tcp(host, port);
    if (!fd_opt.has_value()) throw std::runtime_error("connect failed");
    int fd = *fd_opt;

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Connection: close\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json.size() << "\r\n"
        << "\r\n"
        << json;

    if (!send_all(fd, req.str())) {
        closesock(fd);
        throw std::runtime_error("send failed");
    }

    std::string raw = recv_all(fd);
    closesock(fd);
    return parse_http_response(raw);
}

void usage() {
    std::cout <<
R"(client_cli usage:
  client_cli --host 127.0.0.1 --port 8080 status
  client_cli --host 127.0.0.1 --port 8080 search --q "hello world" [--topk 20]
  client_cli --host 127.0.0.1 --port 8080 build --dataset "/path" --threads 8 [--incremental true|false]
  client_cli --host 127.0.0.1 --port 8080 scheduler --enabled true|false [--interval_s 30]
)";
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8080;

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    auto pop_opt = [&](const std::string& key, std::string& out) {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == key) {
                out = args[i + 1];
                args.erase(args.begin() + (long)i, args.begin() + (long)i + 2);
                return true;
            }
        }
        return false;
    };

    std::string port_s;
    if (pop_opt("--host", host)) {}
    if (pop_opt("--port", port_s)) port = std::stoi(port_s);

    if (args.empty()) {
        usage();
        return 1;
    }

    std::string cmd = args[0];

    try {
        if (cmd == "status") {
            auto r = http_get(host, port, "/status");
            std::cout << r.body << "\n";
            return 0;
        }

        if (cmd == "search") {
            std::string q;
            std::string topk_s;
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "--q" && i + 1 < args.size()) q = args[++i];
                else if (args[i] == "--topk" && i + 1 < args.size()) topk_s = args[++i];
            }
            if (q.empty()) { std::cerr << "Missing --q\n"; return 2; }

            std::string path = "/search?q=" + url_encode(q);
            if (!topk_s.empty()) path += "&topk=" + topk_s;

            auto r = http_get(host, port, path);
            std::cout << r.body << "\n";
            return 0;
        }

        if (cmd == "build") {
            std::string dataset;
            std::string threads_s;
            std::string incremental_s;

            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "--dataset" && i + 1 < args.size()) dataset = args[++i];
                else if (args[i] == "--threads" && i + 1 < args.size()) threads_s = args[++i];
                else if (args[i] == "--incremental" && i + 1 < args.size()) incremental_s = args[++i];
            }

            if (dataset.empty()) { std::cerr << "Missing --dataset\n"; return 2; }
            if (threads_s.empty()) threads_s = "4";
            if (incremental_s.empty()) incremental_s = "true";

            std::ostringstream json;
            json << "{"
                 << "\"dataset_path\":\"" << dataset << "\","
                 << "\"threads\":" << threads_s << ","
                 << "\"incremental\":" << incremental_s
                 << "}";

            auto r = http_post_json(host, port, "/build", json.str());
            std::cout << r.body << "\n";
            return 0;
        }

        if (cmd == "scheduler") {
            std::string enabled_s;
            std::string interval_s;

            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "--enabled" && i + 1 < args.size()) enabled_s = args[++i];
                else if (args[i] == "--interval_s" && i + 1 < args.size()) interval_s = args[++i];
            }

            if (enabled_s.empty()) { std::cerr << "Missing --enabled\n"; return 2; }
            if (interval_s.empty()) interval_s = "30";

            std::ostringstream json;
            json << "{"
                 << "\"enabled\":" << enabled_s << ","
                 << "\"interval_s\":" << interval_s
                 << "}";

            auto r = http_post_json(host, port, "/scheduler", json.str());
            std::cout << r.body << "\n";
            return 0;
        }

        usage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 10;
    }
}
