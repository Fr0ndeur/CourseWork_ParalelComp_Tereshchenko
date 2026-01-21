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

    std::pair<int, bool> get_or_create(const std::string& path,
                                      const std::filesystem::file_time_type& mtime);

    bool contains_path(const std::string& path) const;

    bool needs_indexing(const std::string& path,
                        const std::filesystem::file_time_type& mtime) const;

    void update_mtime(const std::string& path,
                      const std::filesystem::file_time_type& mtime);

    std::optional<std::string> path_for(int doc_id) const;

    std::optional<int> doc_id_for(const std::string& path) const;

    std::vector<DocumentMeta> list_all() const;

    std::size_t size() const;

private:
    mutable std::shared_mutex mu_;
    std::atomic<int> next_id_{1};

    std::unordered_map<std::string, DocumentMeta> by_path_;
    std::unordered_map<int, std::string> by_id_;
};

} // namespace core
