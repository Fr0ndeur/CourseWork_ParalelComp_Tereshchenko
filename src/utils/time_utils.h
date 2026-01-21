#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace utils {

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

struct Stopwatch {
    Stopwatch();

    void reset();

    std::int64_t elapsed_ms() const;
    std::int64_t elapsed_us() const;

private:
    SteadyClock::time_point start_;
};

void sleep_ms(std::int64_t ms);
void sleep_us(std::int64_t us);

SystemClock::time_point now_system();

std::string format_time_local(SystemClock::time_point tp);

std::string now_local_string();

std::string thread_id_string();

} // namespace utils
