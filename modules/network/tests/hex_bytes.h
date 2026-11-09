// SPDX-License-Identifier: AGPL-3.0-only
//
// テストで、ワイヤーフォーマットの期待値を 16 進の文字列で書くためのヘルパー。
// ランデブーメッセージの期待値は、サーバーの実装（infra/rendezvous/internal/udpapi/wire.go）が
// 生成したバイト列をそのまま貼り、クライアントとサーバーの形式が一致することを確かめる。
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace vocroz::network::test {

/// 16 進の文字列をバイト列に変換する。空白は読み飛ばす。
inline std::vector<std::uint8_t> hex_bytes(std::string_view hex) {
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        return static_cast<std::uint8_t>(c - 'a' + 10);
    };
    std::vector<std::uint8_t> out;
    std::uint8_t high = 0;
    bool has_high = false;
    for (const char c : hex) {
        if (c == ' ') {
            continue;
        }
        if (!has_high) {
            high = nibble(c);
            has_high = true;
        } else {
            out.push_back(static_cast<std::uint8_t>((high << 4) | nibble(c)));
            has_high = false;
        }
    }
    return out;
}

}  // namespace vocroz::network::test
