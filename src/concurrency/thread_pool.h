#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "blocking_queue.h"

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool();

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> 
    {
        using R = std::invoke_result_t<F, Args...>;

        if (!accepting_.load(std::memory_order_acquire)) {
            throw std::runtime_error("ThreadPool is not accepting new tasks (shutdown in progress)");
        }

        auto task_ptr = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<R> fut = task_ptr->get_future();

        bool pushed = queue_.push([task_ptr]() {
            (*task_ptr)();
        });

        if (!pushed) {
            throw std::runtime_error("ThreadPool queue is closed");
        }

        return fut;
    }

    void shutdown();

    std::size_t size() const { return workers_.size(); }

private:
    using Job = std::function<void()>;

    void worker_loop();

    std::vector<std::thread> workers_;
    BlockingQueue<Job> queue_;
    std::atomic<bool> accepting_{true};
    std::atomic<bool> started_{false};
};
