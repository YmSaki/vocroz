// SPDX-License-Identifier: AGPL-3.0-only
//
// base64url（パディングなし）の符号化と復号の実装。

#include "wire/base64url.h"

#include <array>
#include <cstddef>

namespace vocroz::network::wire {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/// 6 ビットで表す 1 文字の値。アルファベットにない文字は kInvalid。
constexpr std::uint8_t kInvalid = 0xFF;

constexpr std::array<std::uint8_t, 256> make_decode_table() {
    std::array<std::uint8_t, 256> table{};
    table.fill(kInvalid);
    for (std::size_t i = 0; i < kAlphabet.size(); ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::uint8_t>(i);
    }
    return table;
}

constexpr std::array<std::uint8_t, 256> kDecodeTable = make_decode_table();

}  // namespace

std::string encode_base64url(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() * 4 + 2) / 3);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (static_cast<std::uint32_t>(bytes[i + 1]) << 8) | bytes[i + 2];
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1) {
        const std::uint32_t v = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
    } else if (rest == 2) {
        const std::uint32_t v = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> decode_base64url(std::string_view text) {
    // 4 文字で 3 バイト。余りが 1 文字になる長さは、どのバイト列の符号化にもならない。
    if (text.size() % 4 == 1) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        const std::uint8_t v = kDecodeTable[static_cast<unsigned char>(c)];
        if (v == kInvalid) {
            return std::nullopt;
        }
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    // 余りのビットが 0 でない表記は、同じバイト列の別の表記になるので受け付けない。
    if ((acc & ((1U << bits) - 1)) != 0) {
        return std::nullopt;
    }
    return out;
}

}  // namespace vocroz::network::wire
