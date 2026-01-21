#include "logging.h"

#include "time_utils.h"

#include <iostream>

namespace utils {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(LogLevel lvl) {
    std::lock_guard<std::mutex> lock(mu_);
    level_ = lvl;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mu_);
    return level_;
}

bool Logger::set_log_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    if (path.empty()) {
        file_.reset();
        return true;
    }
    std::ofstream ofs(path, std::ios::out | std::ios::app);
    if (!ofs.is_open()) return false;
    file_.emplace(std::move(ofs));
    return true;
}

std::string Logger::level_name_(LogLevel lvl) const {
    switch (lvl) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        default:              return "INFO";
    }
}

void Logger::log(LogLevel lvl, const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if ((int)lvl < (int)level_) return;
    }

    std::lock_guard<std::mutex> lock(mu_);
    if ((int)lvl < (int)level_) return;

    std::string line =
        "[" + now_local_string() + "]" +
        "[" + level_name_(lvl) + "]" +
        "[tid=" + thread_id_string() + "] " +
        msg;

    std::cout << line << "\n";
    std::cout.flush();

    if (file_.has_value()) {
        (*file_) << line << "\n";
        file_->flush();
    }
}

} // namespace utils
