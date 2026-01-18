#include "time_utils.h"

#include <iomanip>
#include <sstream>
#include <thread>

namespace utils {

Stopwatch::Stopwatch() : start_(SteadyClock::now()) {}
void Stopwatch::reset() { start_ = SteadyClock::now(); }

std::int64_t Stopwatch::elapsed_ms() const {
    auto d = std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - start_);
    return (std::int64_t)d.count();
}

std::int64_t Stopwatch::elapsed_us() const {
    auto d = std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() - start_);
    return (std::int64_t)d.count();
}

void sleep_ms(std::int64_t ms) {
    if (ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void sleep_us(std::int64_t us) {
    if (us <= 0) return;
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

SystemClock::time_point now_system() {
    return SystemClock::now();
}

std::string format_time_local(SystemClock::time_point tp) {
    auto tt = SystemClock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

std::string now_local_string() {
    return format_time_local(now_system());
}

std::string thread_id_string() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

} // namespace utils
