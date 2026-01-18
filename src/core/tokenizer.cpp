#include "tokenizer.h"

#include <cctype>

namespace core {

Tokenizer::Tokenizer(TokenizerConfig cfg) : cfg_(cfg) {}

bool Tokenizer::is_alpha(unsigned char c) {
    return std::isalpha(c) != 0;
}

bool Tokenizer::is_digit(unsigned char c) {
    return std::isdigit(c) != 0;
}

bool Tokenizer::is_token_char(unsigned char c) const {
    if (is_alpha(c)) return true;
    if (cfg_.keep_digits && is_digit(c)) return true;
    return false;
}

char Tokenizer::normalize_char(unsigned char c) const {
    if (cfg_.to_lower) return (char)std::tolower(c);
    return (char)c;
}

std::vector<std::string> Tokenizer::tokenize(std::string_view text) const {
    std::vector<std::string> tokens;
    tokens.reserve(1024);

    std::string cur;
    cur.reserve(32);

    for (unsigned char c : text) {
        if (is_token_char(c)) {
            if (cur.size() < cfg_.max_token_len) {
                cur.push_back(normalize_char(c));
            } else {
                // token too long: keep consuming but do not grow
            }
        } else {
            if (cur.size() >= cfg_.min_token_len) {
                tokens.push_back(std::move(cur));
                cur.clear();
                cur.reserve(32);
            } else {
                cur.clear();
            }
        }
    }

    if (cur.size() >= cfg_.min_token_len) {
        tokens.push_back(std::move(cur));
    }

    return tokens;
}

} // namespace core
