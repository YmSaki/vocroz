// SPDX-License-Identifier: AGPL-3.0-only
//
// base64_utils.h の実装。

#include "detail/base64_utils.h"

#include <array>

namespace vocroz::persistence::detail {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// 1 文字の値。アルファベットになければ -1。
constexpr int kInvalid = -1;

constexpr std::array<int, 256> make_table() {
    std::array<int, 256> table{};
    table.fill(kInvalid);
    for (std::size_t i = 0; i < kAlphabet.size(); ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int>(i);
    }
    return table;
}

constexpr auto kTable = make_table();

int value_of(char c) noexcept {
    return kTable[static_cast<unsigned char>(c)];
}

}  // namespace

std::string encode_base64(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t n =
            (std::uint32_t{bytes[i]} << 16) | (std::uint32_t{bytes[i + 1]} << 8) | bytes[i + 2];
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back(kAlphabet[n & 0x3F]);
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1) {
        const std::uint32_t n = std::uint32_t{bytes[i]} << 16;
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.append("==");
    } else if (rest == 2) {
        const std::uint32_t n =
            (std::uint32_t{bytes[i]} << 16) | (std::uint32_t{bytes[i + 1]} << 8);
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> decode_base64(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        // パディングは最後の 4 文字の、後ろの 1 文字か 2 文字にだけ置ける。
        const std::size_t padding =
            last ? (text[i + 3] == '=' ? (text[i + 2] == '=' ? 2U : 1U) : 0U) : 0U;
        std::uint32_t n = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            if (k >= 4 - padding) {
                n <<= 6;
                continue;
            }
            const int v = value_of(text[i + k]);
            if (v == kInvalid) {
                return std::nullopt;
            }
            n = (n << 6) | static_cast<std::uint32_t>(v);
        }
        out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFF));
        if (padding == 2) {
            if ((n & 0xFFFF) != 0) {
                return std::nullopt;
            }
            break;
        }
        out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        if (padding == 1) {
            if ((n & 0xFF) != 0) {
                return std::nullopt;
            }
            break;
        }
        out.push_back(static_cast<std::uint8_t>(n & 0xFF));
    }
    return out;
}

}  // namespace vocroz::persistence::detail
