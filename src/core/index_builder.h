#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "document_store.h"
#include "file_scanner.h"
#include "inverted_index.h"
#include "tokenizer.h"

namespace core {

struct BuildResult {
    std::size_t scanned_files = 0;
    std::size_t indexed_files = 0;
    std::size_t skipped_files = 0;
    std::size_t errors = 0;
    long long elapsed_ms = 0;
};

class IndexBuilder {
public:
    IndexBuilder(ConcurrentInvertedIndex& index,
                 DocumentStore& store,
                 Tokenizer tokenizer);

    // Full rebuild (indexes everything found).
    BuildResult build_from_directory(const std::string& root_dir,
                                    std::size_t threads);

    // Incremental update: indexes only new/modified files.
    BuildResult update_from_directory(const std::string& root_dir,
                                     std::size_t threads);

    // Index an explicit list of files.
    BuildResult index_files(const std::vector<FileInfo>& files,
                            std::size_t threads,
                            bool incremental);

private:
    ConcurrentInvertedIndex& index_;
    DocumentStore& store_;
    Tokenizer tokenizer_;
    FileScanner scanner_;

    static bool read_file_to_string_(const std::string& path, std::string& out);
    static std::unordered_map<std::string, int> make_term_freq_(const std::vector<std::string>& tokens);
};

} // namespace core
