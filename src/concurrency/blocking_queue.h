#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

template <typename T>
class BlockingQueue {
public:
    BlockingQueue() = default;
    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    bool push(T item) {
        std::unique_lock<std::mutex> lock(mu_);
        if (closed_) return false;
        q_.push_back(std::move(item));
        lock.unlock();
        cv_.notify_one();
        return true;
    }

    template <class... Args>
    bool emplace(Args&&... args) {
        std::unique_lock<std::mutex> lock(mu_);
        if (closed_) return false;
        q_.emplace_back(std::forward<Args>(args)...);
        lock.unlock();
        cv_.notify_one();
        return true;
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mu_);
        if (q_.empty()) return std::nullopt;
        T item = std::move(q_.front());
        q_.pop_front();
        return item;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return closed_ || !q_.empty(); });

        if (q_.empty()) {
            return std::nullopt;
        }

        T item = std::move(q_.front());
        q_.pop_front();
        return item;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mu_);
        return closed_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return q_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mu_);
        return q_.empty();
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> q_;
    bool closed_ = false;
};
