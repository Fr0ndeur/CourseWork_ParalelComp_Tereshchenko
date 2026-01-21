#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/document_store.h"
#include "core/index_builder.h"
#include "core/inverted_index.h"
#include "core/tokenizer.h"
#include "utils/time_utils.h"

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

std::optional<int> connect_tcp(const std::string& host, int port) {
#ifdef _WIN32
    ensure_wsa();
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    std::string port_s = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) return std::nullopt;

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
#ifdef _WIN32
        SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) { fd = (int)s; break; }
        closesocket(s);
#else
        int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        if (connect(s, p->ai_addr, p->ai_addrlen) == 0) { fd = s; break; }
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

std::string http_get_body(const std::string& host, int port, const std::string& path) {
    auto fd_opt = connect_tcp(host, port);
    if (!fd_opt.has_value()) return "";
    int fd = *fd_opt;

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Connection: close\r\n"
        << "\r\n";

    if (!send_all(fd, req.str())) {
        closesock(fd);
        return "";
    }

    std::string raw = recv_all(fd);
    closesock(fd);

    auto p = raw.find("\r\n\r\n");
    if (p == std::string::npos) return "";
    return raw.substr(p + 4);
}

void usage() {
    std::cout << R"USAGE(load_test usage:

Search load (server):
  load_test --mode search --host 127.0.0.1 --port 8080 --clients 50 --duration_s 10 --q "hello" [--topk 20] [--csv out.csv]

Local build benchmark (graphs "time vs threads"):
  load_test --mode build --dataset "/path/to/dataset" --threads_list "1,2,4,8" [--csv build.csv]

Verify sequential vs parallel results (local):
  load_test --mode verify --dataset "/path/to/dataset" --threads_list "2,4,8"
)USAGE";
}


std::vector<int> parse_list(const std::string& s) {
    std::vector<int> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) { out.push_back(std::stoi(cur)); cur.clear(); }
        } else if (!std::isspace((unsigned char)c)) cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(std::stoi(cur));
    if (out.empty()) out.push_back(1);
    return out;
}

std::unordered_map<int, std::string> build_id_path_map(const core::DocumentStore& store) {
    std::unordered_map<int, std::string> out;
    auto docs = store.list_all();
    out.reserve(docs.size());
    for (const auto& d : docs) {
        out[d.doc_id] = d.path;
    }
    return out;
}

uint64_t fnv1a_update(uint64_t hash, const void* data, size_t len) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(p[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t fnv1a_add_string(uint64_t hash, const std::string& s) {
    hash = fnv1a_update(hash, s.data(), s.size());
    const unsigned char sep = 0;
    return fnv1a_update(hash, &sep, 1);
}

uint64_t fnv1a_add_int(uint64_t hash, int v) {
    uint32_t u = static_cast<uint32_t>(v);
    hash = fnv1a_update(hash, &u, sizeof(u));
    const unsigned char sep = 0;
    return fnv1a_update(hash, &sep, 1);
}

uint64_t index_signature(const core::ConcurrentInvertedIndex& index,
                         const core::DocumentStore& store) {
    auto id_to_path = build_id_path_map(store);
    auto snapshot = index.snapshot();

    std::sort(snapshot.begin(), snapshot.end(),
              [](const core::TermPostings& a, const core::TermPostings& b) {
                  return a.term < b.term;
              });

    uint64_t hash = 1469598103934665603ULL;

    for (const auto& tp : snapshot) {
        hash = fnv1a_add_string(hash, tp.term);

        std::vector<std::pair<std::string, int>> by_path;
        by_path.reserve(tp.postings.size());
        for (const auto& p : tp.postings) {
            auto it = id_to_path.find(p.doc_id);
            std::string path = (it != id_to_path.end())
                ? it->second
                : (std::string("<missing:") + std::to_string(p.doc_id) + ">");
            by_path.emplace_back(std::move(path), p.freq);
        }

        std::sort(by_path.begin(), by_path.end(),
                  [](const auto& a, const auto& b) {
                      if (a.first != b.first) return a.first < b.first;
                      return a.second < b.second;
                  });

        for (const auto& pf : by_path) {
            hash = fnv1a_add_string(hash, pf.first);
            hash = fnv1a_add_int(hash, pf.second);
        }
    }

    return hash;
}

} // namespace

int main(int argc, char** argv) {
    std::string mode = "search";
    std::string host = "127.0.0.1";
    int port = 8080;
    int clients = 50;
    int duration_s = 10;
    std::string q = "hello";
    int topk = 20;
    std::string csv_path = "";

    // build mode
    std::string dataset = "";
    std::string threads_list = "1,2,4,8";

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&](std::string& out) { if (i + 1 < argc) out = argv[++i]; };

        if (k == "--mode") next(mode);
        else if (k == "--host") next(host);
        else if (k == "--port") { std::string v; next(v); port = std::stoi(v); }
        else if (k == "--clients") { std::string v; next(v); clients = std::stoi(v); }
        else if (k == "--duration_s") { std::string v; next(v); duration_s = std::stoi(v); }
        else if (k == "--q") next(q);
        else if (k == "--topk") { std::string v; next(v); topk = std::stoi(v); }
        else if (k == "--csv") next(csv_path);
        else if (k == "--dataset") next(dataset);
        else if (k == "--threads_list") next(threads_list);
        else if (k == "--help") { usage(); return 0; }
    }

    if (mode == "build") {
        if (dataset.empty()) {
            std::cerr << "Missing --dataset for build mode\n";
            return 2;
        }

        auto ths = parse_list(threads_list);

        std::ofstream csv;
        if (!csv_path.empty()) {
            csv.open(csv_path);
            csv << "threads,scanned,indexed,skipped,errors,elapsed_ms\n";
        } else {
            std::cout << "threads,scanned,indexed,skipped,errors,elapsed_ms\n";
        }

        for (int t : ths) {
            core::ConcurrentInvertedIndex index(64);
            core::DocumentStore store;
            core::Tokenizer tok(core::TokenizerConfig{true, 2, 64, true});
            core::IndexBuilder builder(index, store, tok);

            auto res = builder.build_from_directory(dataset, (std::size_t)t);

            std::ostringstream line;
            line << t << "," << res.scanned_files << "," << res.indexed_files << ","
                 << res.skipped_files << "," << res.errors << "," << res.elapsed_ms << "\n";

            if (csv.is_open()) csv << line.str();
            else std::cout << line.str();
        }

        return 0;
    }

    if (mode == "verify") {
        if (dataset.empty()) {
            std::cerr << "Missing --dataset for verify mode\n";
            return 2;
        }

        auto ths = parse_list(threads_list);

        core::ConcurrentInvertedIndex ref_index(64);
        core::DocumentStore ref_store;
        core::Tokenizer ref_tok(core::TokenizerConfig{true, 2, 64, true});
        core::IndexBuilder ref_builder(ref_index, ref_store, ref_tok);
        ref_builder.build_from_directory(dataset, 1);

        uint64_t ref_sig = index_signature(ref_index, ref_store);

        bool all_ok = true;
        for (int t : ths) {
            if (t <= 0) t = 1;

            core::ConcurrentInvertedIndex idx(64);
            core::DocumentStore store;
            core::Tokenizer tok(core::TokenizerConfig{true, 2, 64, true});
            core::IndexBuilder builder(idx, store, tok);
            builder.build_from_directory(dataset, (std::size_t)t);

            uint64_t sig = index_signature(idx, store);
            bool ok = (sig == ref_sig);

            std::cout << "verify threads=" << t << " " << (ok ? "ok" : "mismatch") << "\n";
            if (!ok) all_ok = false;
        }

        return all_ok ? 0 : 3;
    }

    // search mode
    std::atomic<bool> stop{false};
    std::atomic<long long> ok_reqs{0};
    std::atomic<long long> fail_reqs{0};

    std::mutex lat_mu;
    std::vector<long long> lat_ms;
    lat_ms.reserve((size_t)clients * 100);

    auto worker = [&](int) {
        while (!stop.load()) {
            utils::Stopwatch sw;

            std::string path = "/search?q=" + url_encode(q) + "&topk=" + std::to_string(topk);
            std::string body = http_get_body(host, port, path);

            long long ms = sw.elapsed_ms();
            {
                std::lock_guard<std::mutex> lk(lat_mu);
                lat_ms.push_back(ms);
            }

            if (!body.empty()) ok_reqs.fetch_add(1);
            else fail_reqs.fetch_add(1);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve((size_t)clients);
    for (int i = 0; i < clients; ++i) threads.emplace_back(worker, i);

    utils::sleep_ms((long long)duration_s * 1000);
    stop.store(true);

    for (auto& t : threads) t.join();

    long long ok = ok_reqs.load();
    long long fail = fail_reqs.load();
    long long total = ok + fail;

    std::sort(lat_ms.begin(), lat_ms.end());
    auto pct = [&](double p) -> long long {
        if (lat_ms.empty()) return 0;
        size_t idx = (size_t)std::min<double>((double)lat_ms.size() - 1, p * (double)(lat_ms.size() - 1));
        return lat_ms[idx];
    };

    double rps = (double)total / (double)duration_s;

    std::ostringstream report;
    report << "mode=search"
           << " clients=" << clients
           << " duration_s=" << duration_s
           << " total=" << total
           << " ok=" << ok
           << " fail=" << fail
           << " rps=" << rps
           << " p50_ms=" << pct(0.50)
           << " p95_ms=" << pct(0.95)
           << " p99_ms=" << pct(0.99)
           << "\n";

    if (!csv_path.empty()) {
        std::ofstream csv(csv_path, std::ios::out | std::ios::trunc);
        csv << "clients,duration_s,total,ok,fail,rps,p50_ms,p95_ms,p99_ms\n";
        csv << clients << "," << duration_s << "," << total << "," << ok << "," << fail << ","
            << rps << "," << pct(0.50) << "," << pct(0.95) << "," << pct(0.99) << "\n";
        std::cout << report.str();
    } else {
        std::cout << report.str();
    }

    return 0;
}
