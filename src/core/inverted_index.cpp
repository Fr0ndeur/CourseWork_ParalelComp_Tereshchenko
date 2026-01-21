#include "inverted_index.h"

#include <algorithm>
#include <unordered_map>

namespace core {

ConcurrentInvertedIndex::ConcurrentInvertedIndex(std::size_t shards)
    : shard_count_(shards ? shards : 1), shards_(shard_count_) {}

std::size_t ConcurrentInvertedIndex::shard_for_(const std::string& term) const {
    return std::hash<std::string>{}(term) % shard_count_;
}

std::vector<std::pair<std::string, int>> ConcurrentInvertedIndex::get_forward_copy_(int doc_id) const {
    std::shared_lock<std::shared_mutex> lock(forward_mu_);
    auto it = forward_.find(doc_id);
    if (it == forward_.end()) return {};
    return it->second;
}

void ConcurrentInvertedIndex::remove_document(int doc_id) {
    // 1) take a copy of forward terms (avoid holding forward lock while locking shards)
    std::vector<std::pair<std::string, int>> terms = get_forward_copy_(doc_id);
    if (terms.empty()) {
        // still erase forward_ entry if exists (could be empty)
        std::unique_lock<std::shared_mutex> wlock(forward_mu_);
        forward_.erase(doc_id);
        return;
    }

    // 2) remove postings for each term
    // group terms by shard to reduce lock churn
    std::unordered_map<std::size_t, std::vector<std::string>> by_shard;
    by_shard.reserve(terms.size());

    for (const auto& kv : terms) {
        const std::string& term = kv.first;
        by_shard[shard_for_(term)].push_back(term);
    }

    for (auto& kv : by_shard) {
        std::size_t sid = kv.first;
        auto& term_list = kv.second;

        std::unique_lock<std::shared_mutex> lock(shards_[sid].mu);
        for (const auto& term : term_list) {
            auto it = shards_[sid].map.find(term);
            if (it == shards_[sid].map.end()) continue;

            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [&](const Posting& p){ return p.doc_id == doc_id; }),
                      vec.end());

            if (vec.empty()) {
                shards_[sid].map.erase(it);
            }
        }
    }

    // 3) remove forward entry
    std::unique_lock<std::shared_mutex> wlock(forward_mu_);
    forward_.erase(doc_id);
}

void ConcurrentInvertedIndex::upsert_document(int doc_id, const std::unordered_map<std::string, int>& term_freq) {
    // Replace semantics: remove old, then add new.
    remove_document(doc_id);

    // Save forward data
    std::vector<std::pair<std::string, int>> forward_terms;
    forward_terms.reserve(term_freq.size());
    for (const auto& kv : term_freq) {
        if (kv.second <= 0) continue;
        forward_terms.emplace_back(kv.first, kv.second);
    }

    {
        std::unique_lock<std::shared_mutex> wlock(forward_mu_);
        forward_[doc_id] = forward_terms;
    }

    // group updates by shard
    std::unordered_map<std::size_t, std::vector<std::pair<std::string, int>>> by_shard;
    by_shard.reserve(forward_terms.size());
    for (const auto& kv : forward_terms) {
        by_shard[shard_for_(kv.first)].push_back(kv);
    }

    for (auto& kv : by_shard) {
        std::size_t sid = kv.first;
        auto& updates = kv.second;

        std::unique_lock<std::shared_mutex> lock(shards_[sid].mu);
        for (const auto& t : updates) {
            const std::string& term = t.first;
            int freq = t.second;
            auto& postings = shards_[sid].map[term];
            postings.push_back(Posting{doc_id, freq});
        }
    }
}

std::vector<SearchResult> ConcurrentInvertedIndex::search(const std::vector<std::string>& query_terms,
                                                         std::size_t top_k) const {
    std::unordered_map<int, double> scores;
    scores.reserve(1024);

    // fetch postings for each term
    for (const auto& term : query_terms) {
        if (term.empty()) continue;
        std::size_t sid = shard_for_(term);

        std::shared_lock<std::shared_mutex> lock(shards_[sid].mu);
        auto it = shards_[sid].map.find(term);
        if (it == shards_[sid].map.end()) continue;

        for (const auto& p : it->second) {
            scores[p.doc_id] += (double)p.freq;
        }
    }

    std::vector<SearchResult> res;
    res.reserve(scores.size());
    for (const auto& kv : scores) {
        res.push_back(SearchResult{kv.first, kv.second});
    }

    std::sort(res.begin(), res.end(), [](const SearchResult& a, const SearchResult& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.doc_id < b.doc_id;
    });

    if (top_k && res.size() > top_k) res.resize(top_k);
    return res;
}

std::vector<TermPostings> ConcurrentInvertedIndex::snapshot() const {
    std::vector<TermPostings> out;

    for (std::size_t i = 0; i < shard_count_; ++i) {
        std::shared_lock<std::shared_mutex> lock(shards_[i].mu);
        for (const auto& kv : shards_[i].map) {
            TermPostings tp;
            tp.term = kv.first;
            tp.postings = kv.second;
            out.push_back(std::move(tp));
        }
    }

    return out;
}

IndexStats ConcurrentInvertedIndex::stats() const {
    IndexStats st{};

    {
        std::shared_lock<std::shared_mutex> lock(forward_mu_);
        st.documents = forward_.size();
    }

    std::size_t terms = 0;
    std::size_t postings = 0;
    for (std::size_t i = 0; i < shard_count_; ++i) {
        std::shared_lock<std::shared_mutex> lock(shards_[i].mu);
        terms += shards_[i].map.size();
        for (const auto& kv : shards_[i].map) {
            postings += kv.second.size();
        }
    }
    st.terms = terms;
    st.postings = postings;
    return st;
}

} // namespace core
