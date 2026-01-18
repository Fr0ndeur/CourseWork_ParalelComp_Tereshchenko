#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace utils {

std::string Config::trim_(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string Config::upper_(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::optional<std::string> Config::getenv_(const std::string& key) {
    const char* v = std::getenv(key.c_str());
    if (!v) return std::nullopt;
    return std::string(v);
}

bool Config::load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        line = trim_(line);
        if (line.empty()) continue;
        if (!line.empty() && line[0] == '#') continue;

        // KEY=VALUE
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim_(line.substr(0, eq));
        std::string val = trim_(line.substr(eq + 1));

        if (key.empty()) continue;

        // Strip quotes if "..." or '...'
        if (val.size() >= 2) {
            if ((val.front() == '"' && val.back() == '"') ||
                (val.front() == '\'' && val.back() == '\'')) {
                val = val.substr(1, val.size() - 2);
            }
        }

        kv_[upper_(key)] = val;
    }

    return true;
}

void Config::set(const std::string& key, const std::string& value) {
    kv_[upper_(key)] = value;
}

bool Config::has(const std::string& key) const {
    auto k = upper_(key);
    if (getenv_(k).has_value()) return true;
    return kv_.find(k) != kv_.end();
}

std::optional<std::string> Config::get_string_opt(const std::string& key) const {
    auto k = upper_(key);

    if (auto env = getenv_(k); env.has_value()) {
        return env;
    }
    auto it = kv_.find(k);
    if (it == kv_.end()) return std::nullopt;
    return it->second;
}

std::string Config::get_string(const std::string& key, const std::string& default_value) const {
    auto opt = get_string_opt(key);
    return opt.has_value() ? *opt : default_value;
}

int Config::get_int(const std::string& key, int default_value) const {
    auto s = get_string_opt(key);
    if (!s.has_value()) return default_value;
    try {
        return std::stoi(*s);
    } catch (...) {
        return default_value;
    }
}

bool Config::get_bool(const std::string& key, bool default_value) const {
    auto s = get_string_opt(key);
    if (!s.has_value()) return default_value;

    std::string v = *s;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return (char)std::tolower(c); });

    if (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "n" || v == "off") return false;
    return default_value;
}

} // namespace utils
