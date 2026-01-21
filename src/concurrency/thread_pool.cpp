#include "thread_pool.h"

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        thread_count = 1;
    }

    started_.store(true, std::memory_order_release);

    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    bool expected = true;
    if (!accepting_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    queue_.close();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void ThreadPool::worker_loop() {
    while (true) {
        auto job_opt = queue_.pop();
        if (!job_opt.has_value()) {
            break;
        }

        try {
            (*job_opt)();
        } catch (...) {
        }
    }
}
