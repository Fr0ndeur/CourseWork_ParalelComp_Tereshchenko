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

struct TermPostings {
    std::string term;
    std::vector<Posting> postings;
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

    void upsert_document(int doc_id, const std::unordered_map<std::string, int>& term_freq);

    void remove_document(int doc_id);

    std::vector<SearchResult> search(const std::vector<std::string>& query_terms,
                                    std::size_t top_k = 20) const;

    std::vector<TermPostings> snapshot() const;

    IndexStats stats() const;

private:
    struct Shard {
        mutable std::shared_mutex mu;
        std::unordered_map<std::string, std::vector<Posting>> map;
    };

    std::size_t shard_count_;
    std::vector<Shard> shards_;

    mutable std::shared_mutex forward_mu_;
    std::unordered_map<int, std::vector<std::pair<std::string, int>>> forward_;

    std::size_t shard_for_(const std::string& term) const;

    std::vector<std::pair<std::string, int>> get_forward_copy_(int doc_id) const;
};

} // namespace core
