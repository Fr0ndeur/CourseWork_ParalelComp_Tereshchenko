#pragma once

#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <filesystem>

namespace core {

struct DocumentMeta {
    int doc_id = -1;
    std::string path;
    std::filesystem::file_time_type mtime{};
};

class DocumentStore {
public:
    DocumentStore();

    // Get existing doc_id by path; otherwise create new doc_id and store meta.
    // Returns {doc_id, created_new}
    std::pair<int, bool> get_or_create(const std::string& path,
                                      const std::filesystem::file_time_type& mtime);

    // Returns true if we have this path
    bool contains_path(const std::string& path) const;

    // Check if file is new/modified vs stored mtime.
    // If path doesn't exist in store -> returns true (needs indexing).
    bool needs_indexing(const std::string& path,
                        const std::filesystem::file_time_type& mtime) const;

    // Update mtime for a path (must exist)
    void update_mtime(const std::string& path,
                      const std::filesystem::file_time_type& mtime);

    // Resolve doc_id -> path
    std::optional<std::string> path_for(int doc_id) const;

    // Resolve path -> doc_id
    std::optional<int> doc_id_for(const std::string& path) const;

    // List all documents (snapshot)
    std::vector<DocumentMeta> list_all() const;

    std::size_t size() const;

private:
    mutable std::shared_mutex mu_;
    std::atomic<int> next_id_{1};

    std::unordered_map<std::string, DocumentMeta> by_path_; // path -> meta
    std::unordered_map<int, std::string> by_id_;            // id -> path
};

} // namespace core
