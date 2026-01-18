#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace utils {

// Simple config loader:
// - reads KEY=VALUE lines from a file (.env style)
// - ignores empty lines and comments starting with #
// - values can be unquoted or quoted ("...") / ('...')
// - environment variables override file values if present
class Config {
public:
    // Load from file (optional). Returns false if file not found or can't open.
    bool load_file(const std::string& path);

    // Set manually
    void set(const std::string& key, const std::string& value);

    // Get raw string (env override -> file -> default)
    std::string get_string(const std::string& key, const std::string& default_value = "") const;

    std::optional<std::string> get_string_opt(const std::string& key) const;

    int get_int(const std::string& key, int default_value) const;
    bool get_bool(const std::string& key, bool default_value) const;

    // Debug
    bool has(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> kv_;

    static std::string trim_(std::string s);
    static std::string upper_(std::string s);
    static std::optional<std::string> getenv_(const std::string& key);
};

} // namespace utils
