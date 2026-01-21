#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "core/document_store.h"
#include "core/index_builder.h"
#include "core/inverted_index.h"
#include "core/tokenizer.h"
#include "net/http_server.h"
#include "net/request_router.h"
#include "net/json_min.h"
#include "utils/config.h"
#include "utils/logging.h"
#include "utils/time_utils.h"

namespace {

std::string escape_json(std::string_view s) { return json_min::escape_json(s); }

std::optional<bool> parse_bool_token(const std::string& token) {
    std::string v = token;
    for (char& c : v) c = (char)std::tolower((unsigned char)c);
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return std::nullopt;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string guess_content_type(const std::string& path) {
    std::filesystem::path p(path);
    auto ext = p.extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);

    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    return "text/plain; charset=utf-8";
}

struct BuildJob {
    std::atomic<bool> running{false};
    std::atomic<bool> has_result{false};
    std::mutex mu;
    core::BuildResult last{};
    std::string last_mode; // "build" or "update"
    std::string last_dataset;
    std::size_t last_threads = 0;
    std::string last_error;
};

struct AppState {
    core::ConcurrentInvertedIndex index{64};
    core::DocumentStore store;
    core::Tokenizer tokenizer{ core::TokenizerConfig{true, 2, 64, true} };
    core::IndexBuilder builder{ index, store, tokenizer };

    std::string dataset_path = "";
    std::size_t build_threads = 4;

    std::string web_root = "web";

    std::atomic<bool> scheduler_enabled{false};
    std::atomic<bool> stop_scheduler{false};
    std::size_t scheduler_interval_s = 30;

    BuildJob job;
};

std::string stats_json(AppState& st) {
    auto idx = st.index.stats();
    std::ostringstream oss;

    bool building = st.job.running.load();

    std::string last_build = "null";
    std::string last_error = "null";
    std::string last_mode  = "null";
    std::string last_ds    = "null";
    std::size_t last_threads = 0;
    core::BuildResult last_res{};

    if (st.job.has_result.load()) {
        std::lock_guard<std::mutex> lk(st.job.mu);
        last_res = st.job.last;
        last_threads = st.job.last_threads;
        last_mode = "\"" + escape_json(st.job.last_mode) + "\"";
        last_ds   = "\"" + escape_json(st.job.last_dataset) + "\"";
        last_error = st.job.last_error.empty() ? "null" : ("\"" + escape_json(st.job.last_error) + "\"");
        last_build = "{"
            "\"scanned_files\":" + std::to_string(last_res.scanned_files) + ","
            "\"indexed_files\":" + std::to_string(last_res.indexed_files) + ","
            "\"skipped_files\":" + std::to_string(last_res.skipped_files) + ","
            "\"errors\":" + std::to_string(last_res.errors) + ","
            "\"elapsed_ms\":" + std::to_string(last_res.elapsed_ms) +
        "}";
    }

    oss
      << "{"
      << "\"ok\":true,"
      << "\"building\":" << (building ? "true" : "false") << ","
      << "\"dataset_path\":\"" << escape_json(st.dataset_path) << "\","
      << "\"build_threads\":" << st.build_threads << ","
      << "\"scheduler_enabled\":" << (st.scheduler_enabled.load() ? "true" : "false") << ","
      << "\"scheduler_interval_s\":" << st.scheduler_interval_s << ","
      << "\"index\":{"
          << "\"documents\":" << idx.documents << ","
          << "\"terms\":" << idx.terms << ","
          << "\"postings\":" << idx.postings
      << "},"
      << "\"last\":{"
          << "\"mode\":" << last_mode << ","
          << "\"dataset\":" << last_ds << ","
          << "\"threads\":" << last_threads << ","
          << "\"result\":" << last_build << ","
          << "\"error\":" << last_error
      << "}"
      << "}";

    return oss.str();
}

net::HttpResponse serve_static(const AppState& st, const std::string& rel_path) {
    std::filesystem::path p = std::filesystem::path(st.web_root) / rel_path;
    std::string body;
    if (!read_file(p.string(), body)) {
        return net::make_text_response(404, "Not Found");
    }
    net::HttpResponse r;
    r.status = 200;
    r.reason = "OK";
    r.headers["Content-Type"] = guess_content_type(p.string());
    r.body = std::move(body);
    return r;
}

void start_build_job(AppState& st, std::string dataset_path, std::size_t threads, bool incremental) {
    if (st.job.running.exchange(true)) {
        return; // already running
    }

    st.job.has_result.store(false);

    std::thread([&st, dataset_path = std::move(dataset_path), threads, incremental]() mutable {
        try {
            utils::Logger::instance().info(
                std::string("Build job started: mode=") + (incremental ? "update" : "build") +
                " dataset=" + dataset_path + " threads=" + std::to_string(threads)
            );

            core::BuildResult res;
            if (incremental) {
                res = st.builder.update_from_directory(dataset_path, threads);
            } else {
                res = st.builder.build_from_directory(dataset_path, threads);
            }

            {
                std::lock_guard<std::mutex> lk(st.job.mu);
                st.job.last = res;
                st.job.last_mode = incremental ? "update" : "build";
                st.job.last_dataset = dataset_path;
                st.job.last_threads = threads;
                st.job.last_error.clear();
            }
            st.job.has_result.store(true);

            utils::Logger::instance().info("Build job finished OK");
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lk(st.job.mu);
                st.job.last_error = e.what();
                st.job.last_mode = incremental ? "update" : "build";
                st.job.last_dataset = dataset_path;
                st.job.last_threads = threads;
            }
            st.job.has_result.store(true);
            utils::Logger::instance().error(std::string("Build job failed: ") + e.what());
        } catch (...) {
            {
                std::lock_guard<std::mutex> lk(st.job.mu);
                st.job.last_error = "unknown_error";
                st.job.last_mode = incremental ? "update" : "build";
                st.job.last_dataset = dataset_path;
                st.job.last_threads = threads;
            }
            st.job.has_result.store(true);
            utils::Logger::instance().error("Build job failed: unknown_error");
        }

        st.job.running.store(false);
    }).detach();
}

struct Args {
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string dataset = "";
    std::size_t threads = 4;
    std::string web_root = "web";
    bool scheduler = false;
    std::size_t sched_s = 30;
    std::string config_path = "config.env";
    std::string log_file = "";
    std::string log_level = "info";
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) out = argv[++i];
        };
        if (k == "--host") next(a.host);
        else if (k == "--port") { std::string v; next(v); a.port = std::stoi(v); }
        else if (k == "--dataset") next(a.dataset);
        else if (k == "--threads") { std::string v; next(v); a.threads = (std::size_t)std::stoul(v); }
        else if (k == "--web_root") next(a.web_root);
        else if (k == "--scheduler") a.scheduler = true;
        else if (k == "--sched_s") { std::string v; next(v); a.sched_s = (std::size_t)std::stoul(v); }
        else if (k == "--config") next(a.config_path);
        else if (k == "--log_file") next(a.log_file);
        else if (k == "--log_level") next(a.log_level);
    }
    return a;
}

utils::LogLevel parse_level(const std::string& s) {
    std::string v = s;
    for (char& c : v) c = (char)std::tolower((unsigned char)c);
    if (v == "trace") return utils::LogLevel::Trace;
    if (v == "debug") return utils::LogLevel::Debug;
    if (v == "warn")  return utils::LogLevel::Warn;
    if (v == "error") return utils::LogLevel::Error;
    return utils::LogLevel::Info;
}

} // namespace

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);

    utils::Config cfg;
    cfg.load_file(args.config_path);

    utils::Logger::instance().set_level(parse_level(cfg.get_string("LOG_LEVEL", args.log_level)));
    {
        std::string lf = cfg.get_string("LOG_FILE", args.log_file);
        if (!lf.empty()) {
            if (!utils::Logger::instance().set_log_file(lf)) {
                std::cerr << "Failed to open log file: " << lf << "\n";
            }
        }
    }

    AppState st;
    st.dataset_path = cfg.get_string("DATASET_PATH", args.dataset);
    st.build_threads = (std::size_t)cfg.get_int("BUILD_THREADS", (int)args.threads);
    st.web_root = cfg.get_string("WEB_ROOT", args.web_root);
    st.scheduler_interval_s = (std::size_t)cfg.get_int("SCHED_INTERVAL_S", (int)args.sched_s);

    bool sched_on = cfg.get_bool("SCHED_ENABLED", args.scheduler);
    st.scheduler_enabled.store(sched_on);

    std::thread sched_thread([&]() {
        while (!st.stop_scheduler.load()) {
            utils::sleep_ms((long long)st.scheduler_interval_s * 1000);

            if (st.stop_scheduler.load()) break;
            if (!st.scheduler_enabled.load()) continue;
            if (st.dataset_path.empty()) continue;
            if (st.job.running.load()) continue;

            start_build_job(st, st.dataset_path, st.build_threads, /*incremental=*/true);
        }
    });

    net::RequestRouter router;

    router.add_route("GET", "/", [&](const net::HttpRequest&) {
        return serve_static(st, "index.html");
    });
    router.add_route("GET", "/app.js", [&](const net::HttpRequest&) {
        return serve_static(st, "app.js");
    });
    router.add_route("GET", "/styles.css", [&](const net::HttpRequest&) {
        return serve_static(st, "styles.css");
    });

    router.add_route("GET", "/status", [&](const net::HttpRequest&) {
        return net::make_json_response(200, stats_json(st));
    });

    router.add_route("GET", "/search", [&](const net::HttpRequest& req) {
        auto it = req.query.find("q");
        std::string q = (it == req.query.end()) ? "" : it->second;

        std::size_t topk = 20;
        auto it2 = req.query.find("topk");
        if (it2 != req.query.end()) {
            try { topk = (std::size_t)std::stoul(it2->second); } catch (...) {}
        }

        utils::Stopwatch sw;
        auto terms = st.tokenizer.tokenize(q);
        auto results = st.index.search(terms, topk);

        std::ostringstream oss;
        oss << "{"
            << "\"ok\":true,"
            << "\"q\":\"" << escape_json(q) << "\","
            << "\"terms\":[";
        for (std::size_t i = 0; i < terms.size(); ++i) {
            if (i) oss << ",";
            oss << "\"" << escape_json(terms[i]) << "\"";
        }
        oss << "],"
            << "\"t_ms\":" << sw.elapsed_ms() << ","
            << "\"results\":[";
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (i) oss << ",";
            auto path_opt = st.store.path_for(results[i].doc_id);
            std::string path = path_opt.has_value() ? *path_opt : "";
            oss << "{"
                << "\"doc_id\":" << results[i].doc_id << ","
                << "\"score\":" << results[i].score << ","
                << "\"path\":\"" << escape_json(path) << "\""
                << "}";
        }
        oss << "]}";

        return net::make_json_response(200, oss.str());
    });

    router.add_route("POST", "/build", [&](const net::HttpRequest& req) {
        json_min::Object obj;
        std::string err;
        if (!json_min::parse_object(req.body, obj, &err)) {
            return net::make_json_response(400,
                std::string(R"({"ok":false,"error":"bad_json","details":")") + escape_json(err) + R"("})");
        }

        std::string dataset = json_min::get_string(obj, "dataset_path").value_or(st.dataset_path);
        long long threads_ll = json_min::get_int(obj, "threads").value_or((long long)st.build_threads);
        if (threads_ll <= 0) threads_ll = 1;

        bool incremental = true;
        if (auto inc_s = json_min::get_string(obj, "incremental"); inc_s.has_value()) {
            if (auto b = parse_bool_token(*inc_s); b.has_value()) incremental = *b;
        }

        if (dataset.empty()) {
            return net::make_json_response(400, R"({"ok":false,"error":"dataset_path_required"})");
        }

        // Update defaults
        st.dataset_path = dataset;
        st.build_threads = (std::size_t)threads_ll;

        if (st.job.running.load()) {
            return net::make_json_response(200, R"({"ok":true,"status":"already_running"})");
        }

        start_build_job(st, dataset, (std::size_t)threads_ll, incremental);

        return net::make_json_response(200,
            std::string(R"({"ok":true,"status":"started","mode":")") +
            (incremental ? "update" : "build") + R"(","dataset_path":")" +
            escape_json(dataset) + R"(","threads":)" + std::to_string(threads_ll) + "}"
        );
    });

    router.add_route("POST", "/scheduler", [&](const net::HttpRequest& req) {
        json_min::Object obj;
        std::string err;
        if (!json_min::parse_object(req.body, obj, &err)) {
            return net::make_json_response(400,
                std::string(R"({"ok":false,"error":"bad_json","details":")") + escape_json(err) + R"("})");
        }

        bool enabled = st.scheduler_enabled.load();
        if (auto v = json_min::get_string(obj, "enabled"); v.has_value()) {
            if (auto b = parse_bool_token(*v); b.has_value()) enabled = *b;
        }
        long long interval = (long long)st.scheduler_interval_s;
        if (auto v = json_min::get_int(obj, "interval_s"); v.has_value()) {
            if (*v > 0) interval = *v;
        }

        st.scheduler_enabled.store(enabled);
        st.scheduler_interval_s = (std::size_t)interval;

        return net::make_json_response(200,
            std::string(R"({"ok":true,"enabled":)") + (enabled ? "true" : "false") +
            R"(,"interval_s":)" + std::to_string(interval) + "}"
        );
    });

    net::HttpServer server(args.host, (uint16_t)args.port, [&](const net::HttpRequest& req) {
        return router.route(req);
    });

    try {
        server.run();
    } catch (const std::exception& e) {
        utils::Logger::instance().error(std::string("Server crashed: ") + e.what());
    }

    st.stop_scheduler.store(true);
    if (sched_thread.joinable()) sched_thread.join();
    return 0;
}
