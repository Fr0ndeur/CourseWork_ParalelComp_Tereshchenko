#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace core {

struct TokenizerConfig {
    bool to_lower = true;
    std::size_t min_token_len = 2;
    std::size_t max_token_len = 64;
    bool keep_digits = true;   // keep 0-9
};

class Tokenizer {
public:
    explicit Tokenizer(TokenizerConfig cfg = {});

    // Tokenize whole text into vector of tokens
    std::vector<std::string> tokenize(std::string_view text) const;

private:
    TokenizerConfig cfg_;

    static bool is_alpha(unsigned char c);
    static bool is_digit(unsigned char c);
    bool is_token_char(unsigned char c) const;
    char normalize_char(unsigned char c) const;
};

} // namespace core
