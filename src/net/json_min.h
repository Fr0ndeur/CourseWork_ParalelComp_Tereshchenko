#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace json_min {

struct Object {
    // For strings: stored as decoded string (без кавычек)
    // For numbers/bool/null: stored as raw token (например "8", "true", "null")
    std::unordered_map<std::string, std::string> kv;
};

inline void skip_ws(std::string_view s, size_t& i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
}

inline bool consume(std::string_view s, size_t& i, char ch) {
    skip_ws(s, i);
    if (i < s.size() && s[i] == ch) { ++i; return true; }
    return false;
}

inline std::optional<std::string> parse_string(std::string_view s, size_t& i) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '"') return std::nullopt;
    ++i;
    std::string out;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return out;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default:  out.push_back(e); break; // simplistic
            }
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

inline std::optional<std::string> parse_token(std::string_view s, size_t& i) {
    skip_ws(s, i);
    if (i >= s.size()) return std::nullopt;

    // string
    if (s[i] == '"') return parse_string(s, i);

    // number / true / false / null
    size_t start = i;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace((unsigned char)c) || c == ',' || c == '}' ) break;
        ++i;
    }
    if (i == start) return std::nullopt;
    return std::string(s.substr(start, i - start));
}

inline bool parse_object(std::string_view s, Object& out, std::string* err = nullptr) {
    size_t i = 0;
    skip_ws(s, i);
    if (!consume(s, i, '{')) {
        if (err) *err = "expected {";
        return false;
    }

    skip_ws(s, i);
    if (consume(s, i, '}')) {
        return true; // empty object
    }

    while (i < s.size()) {
        auto k = parse_string(s, i);
        if (!k.has_value()) {
            if (err) *err = "expected string key";
            return false;
        }

        if (!consume(s, i, ':')) {
            if (err) *err = "expected :";
            return false;
        }

        // value can be string (returned already) OR token (number/bool/null)
        skip_ws(s, i);
        std::string val;
        if (i < s.size() && s[i] == '"') {
            auto sv = parse_string(s, i);
            if (!sv.has_value()) {
                if (err) *err = "bad string value";
                return false;
            }
            val = *sv;
        } else {
            auto tv = parse_token(s, i);
            if (!tv.has_value()) {
                if (err) *err = "expected value token";
                return false;
            }
            val = *tv;
        }

        out.kv[*k] = val;

        skip_ws(s, i);
        if (consume(s, i, '}')) {
            return true;
        }
        if (!consume(s, i, ',')) {
            if (err) *err = "expected , or }";
            return false;
        }
    }

    if (err) *err = "unexpected end";
    return false;
}

inline std::optional<std::string> get_string(const Object& o, const std::string& key) {
    auto it = o.kv.find(key);
    if (it == o.kv.end()) return std::nullopt;
    return it->second; // already decoded string (если строка) либо токен (если не строка)
}

inline std::optional<long long> get_int(const Object& o, const std::string& key) {
    auto it = o.kv.find(key);
    if (it == o.kv.end()) return std::nullopt;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return std::nullopt;
    }
}

inline std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

} // namespace json_min
