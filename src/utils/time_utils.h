#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace utils {

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

struct Stopwatch {
    Stopwatch();

    // reset start time to now
    void reset();

    // elapsed time since last reset/start
    std::int64_t elapsed_ms() const;
    std::int64_t elapsed_us() const;

private:
    SteadyClock::time_point start_;
};

// Sleep helpers
void sleep_ms(std::int64_t ms);
void sleep_us(std::int64_t us);

// Current time
SystemClock::time_point now_system();

// Format current/local time as ISO-ish string: "YYYY-MM-DD HH:MM:SS.mmm"
std::string format_time_local(SystemClock::time_point tp);

// Format now in local time
std::string now_local_string();

// Thread id as string (for logging)
std::string thread_id_string();

} // namespace utils
