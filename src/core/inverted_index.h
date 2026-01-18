#pragma once

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

struct Posting {
    int doc_id = -1;
    int freq = 0;
};

struct SearchResult {
    int doc_id = -1;
    double score = 0.0;
};

struct IndexStats {
    std::size_t documents = 0;     // how many docs tracked in forward index
    std::size_t terms = 0;         // number of unique terms
    std::size_t postings = 0;      // total postings across all terms
};

class ConcurrentInvertedIndex {
public:
    explicit ConcurrentInvertedIndex(std::size_t shards = 64);

    ConcurrentInvertedIndex(const ConcurrentInvertedIndex&) = delete;
    ConcurrentInvertedIndex& operator=(const ConcurrentInvertedIndex&) = delete;

    // Add/update document with provided term frequencies.
    // If doc already exists -> it will be replaced (old postings removed).
    void upsert_document(int doc_id, const std::unordered_map<std::string, int>& term_freq);

    // Remove document from index (no-op if absent).
    void remove_document(int doc_id);

    // Search: score = sum(freq) across query terms (simple baseline).
    // Returns top_k docs sorted by score desc.
    std::vector<SearchResult> search(const std::vector<std::string>& query_terms,
                                    std::size_t top_k = 20) const;

    IndexStats stats() const;

private:
    struct Shard {
        mutable std::shared_mutex mu;
        std::unordered_map<std::string, std::vector<Posting>> map;
    };

    std::size_t shard_count_;
    std::vector<Shard> shards_;

    // Forward index to support efficient remove/update:
    // doc_id -> list of (term, freq)
    mutable std::shared_mutex forward_mu_;
    std::unordered_map<int, std::vector<std::pair<std::string, int>>> forward_;

    std::size_t shard_for_(const std::string& term) const;

    // internal helpers (assume forward lock handled by caller appropriately)
    std::vector<std::pair<std::string, int>> get_forward_copy_(int doc_id) const;
};

} // namespace core
