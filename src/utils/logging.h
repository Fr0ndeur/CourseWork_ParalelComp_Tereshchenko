#pragma once

#include <fstream>
#include <mutex>
#include <optional>
#include <string>

namespace utils {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

class Logger {
public:
    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_level(LogLevel lvl);
    LogLevel level() const;

    // Optional: log to file as well as stdout
    // If path empty -> disables file output
    bool set_log_file(const std::string& path);

    // Log raw message at level
    void log(LogLevel lvl, const std::string& msg);

    // Convenience wrappers
    void trace(const std::string& msg) { log(LogLevel::Trace, msg); }
    void debug(const std::string& msg) { log(LogLevel::Debug, msg); }
    void info (const std::string& msg) { log(LogLevel::Info,  msg); }
    void warn (const std::string& msg) { log(LogLevel::Warn,  msg); }
    void error(const std::string& msg) { log(LogLevel::Error, msg); }

private:
    Logger() = default;

    std::string level_name_(LogLevel lvl) const;

    mutable std::mutex mu_;
    LogLevel level_ = LogLevel::Info;
    std::optional<std::ofstream> file_;
};

// Helper macros (optional but nice for clean code)
#define LOG_TRACE(msg) ::utils::Logger::instance().trace(msg)
#define LOG_DEBUG(msg) ::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg)  ::utils::Logger::instance().info(msg)
#define LOG_WARN(msg)  ::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) ::utils::Logger::instance().error(msg)

} // namespace utils
