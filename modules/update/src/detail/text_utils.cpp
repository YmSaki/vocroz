// SPDX-License-Identifier: AGPL-3.0-only
//
// text_utils.h の実装。

#include "detail/text_utils.h"

namespace vocroz::update::detail {
namespace {

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

}  // namespace

std::vector<std::string_view> split(std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find(delimiter, begin);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(begin));
            return parts;
        }
        parts.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
}

bool is_all_digits(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

bool decode_lower_hex(std::string_view text, std::span<std::uint8_t> out) noexcept {
    if (text.size() != out.size() * 2) {
        return false;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int high = hex_value(text[i * 2]);
        const int low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

std::string encode_lower_hex(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0F];
    }
    return text;
}

std::optional<std::string_view> value_of(std::string_view line, std::string_view key) noexcept {
    if (line.size() <= key.size() || line.substr(0, key.size()) != key || line[key.size()] != '=') {
        return std::nullopt;
    }
    return line.substr(key.size() + 1);
}

}  // namespace vocroz::update::detail
