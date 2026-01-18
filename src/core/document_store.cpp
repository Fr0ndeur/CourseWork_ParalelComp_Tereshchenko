#include "document_store.h"

namespace core {

DocumentStore::DocumentStore() = default;

std::pair<int, bool> DocumentStore::get_or_create(const std::string& path,
                                                  const std::filesystem::file_time_type& mtime) {
    {
        std::shared_lock<std::shared_mutex> rlock(mu_);
        auto it = by_path_.find(path);
        if (it != by_path_.end()) {
            return {it->second.doc_id, false};
        }
    }

    std::unique_lock<std::shared_mutex> wlock(mu_);
    auto it2 = by_path_.find(path);
    if (it2 != by_path_.end()) {
        return {it2->second.doc_id, false};
    }

    int id = next_id_.fetch_add(1);
    DocumentMeta meta;
    meta.doc_id = id;
    meta.path = path;
    meta.mtime = mtime;

    by_path_[path] = meta;
    by_id_[id] = path;

    return {id, true};
}

bool DocumentStore::contains_path(const std::string& path) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return by_path_.find(path) != by_path_.end();
}

bool DocumentStore::needs_indexing(const std::string& path,
                                   const std::filesystem::file_time_type& mtime) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = by_path_.find(path);
    if (it == by_path_.end()) return true;
    return mtime > it->second.mtime;
}

void DocumentStore::update_mtime(const std::string& path,
                                 const std::filesystem::file_time_type& mtime) {
    std::unique_lock<std::shared_mutex> lock(mu_);
    auto it = by_path_.find(path);
    if (it != by_path_.end()) {
        it->second.mtime = mtime;
    }
}

std::optional<std::string> DocumentStore::path_for(int doc_id) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = by_id_.find(doc_id);
    if (it == by_id_.end()) return std::nullopt;
    return it->second;
}

std::optional<int> DocumentStore::doc_id_for(const std::string& path) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = by_path_.find(path);
    if (it == by_path_.end()) return std::nullopt;
    return it->second.doc_id;
}

std::vector<DocumentMeta> DocumentStore::list_all() const {
    std::vector<DocumentMeta> out;
    std::shared_lock<std::shared_mutex> lock(mu_);
    out.reserve(by_path_.size());
    for (const auto& kv : by_path_) {
        out.push_back(kv.second);
    }
    return out;
}

std::size_t DocumentStore::size() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return by_path_.size();
}

} // namespace core
