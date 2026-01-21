#include "index_builder.h"

#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "concurrency/thread_pool.h"
#include "utils/logging.h"
#include "utils/time_utils.h"

namespace core {

IndexBuilder::IndexBuilder(ConcurrentInvertedIndex& index,
                           DocumentStore& store,
                           Tokenizer tokenizer)
    : index_(index), store_(store), tokenizer_(std::move(tokenizer)), scanner_(ScanConfig{true, true, 0}) {}

bool IndexBuilder::read_file_to_string_(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    in.seekg(0, std::ios::end);
    std::streamoff sz = in.tellg();
    if (sz < 0) sz = 0;
    in.seekg(0, std::ios::beg);

    out.clear();
    out.reserve((std::size_t)sz);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::unordered_map<std::string, int> IndexBuilder::make_term_freq_(const std::vector<std::string>& tokens) {
    std::unordered_map<std::string, int> tf;
    tf.reserve(tokens.size() / 2 + 16);
    for (const auto& t : tokens) {
        if (t.empty()) continue;
        tf[t] += 1;
    }
    return tf;
}

BuildResult IndexBuilder::build_from_directory(const std::string& root_dir,
                                              std::size_t threads) {
    auto files = scanner_.scan(root_dir);
    return index_files(files, threads, /*incremental=*/false);
}

BuildResult IndexBuilder::update_from_directory(const std::string& root_dir,
                                               std::size_t threads) {
    auto files = scanner_.scan(root_dir);
    return index_files(files, threads, /*incremental=*/true);
}

BuildResult IndexBuilder::index_files(const std::vector<FileInfo>& files,
                                      std::size_t threads,
                                      bool incremental) {
    using utils::Logger;
    using utils::Stopwatch;

    BuildResult r;
    r.scanned_files = files.size();

    Stopwatch sw;

    if (threads == 0) threads = 1;

    ThreadPool pool(threads);

    std::mutex agg_mu;
    std::size_t indexed = 0;
    std::size_t skipped = 0;
    std::size_t errors = 0;

    std::vector<std::future<void>> futs;
    futs.reserve(files.size());

    for (const auto& fi : files) {
        futs.push_back(pool.submit([&, fi]() {
            try {
                if (incremental) {
                    if (!store_.needs_indexing(fi.path, fi.mtime)) {
                        std::lock_guard<std::mutex> lk(agg_mu);
                        skipped++;
                        return;
                    }
                }

                std::string text;
                if (!read_file_to_string_(fi.path, text)) {
                    std::lock_guard<std::mutex> lk(agg_mu);
                    errors++;
                    return;
                }

                auto tokens = tokenizer_.tokenize(text);
                auto tf = make_term_freq_(tokens);

                auto [doc_id, created] = store_.get_or_create(fi.path, fi.mtime);

                index_.upsert_document(doc_id, tf);

                store_.update_mtime(fi.path, fi.mtime);

                std::lock_guard<std::mutex> lk(agg_mu);
                indexed++;
            } catch (...) {
                std::lock_guard<std::mutex> lk(agg_mu);
                errors++;
            }
        }));
    }

    for (auto& f : futs) {
        try { f.get(); } catch (...) { /* counted above or ignore */ }
    }

    pool.shutdown();

    r.indexed_files = indexed;
    r.skipped_files = skipped;
    r.errors = errors;
    r.elapsed_ms = sw.elapsed_ms();

    Logger::instance().info(
        "IndexBuilder done: scanned=" + std::to_string(r.scanned_files) +
        " indexed=" + std::to_string(r.indexed_files) +
        " skipped=" + std::to_string(r.skipped_files) +
        " errors=" + std::to_string(r.errors) +
        " t_ms=" + std::to_string(r.elapsed_ms)
    );

    return r;
}

} // namespace core
