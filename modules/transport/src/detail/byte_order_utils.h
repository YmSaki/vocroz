// SPDX-License-Identifier: AGPL-3.0-only
//
// ワイヤーフォーマットの整数をビッグエンディアン（ネットワークバイトオーダー）で読み書きする。
// ホストのバイトオーダーやアラインメントに依存しないよう、1 バイトずつシフトで組み立てる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace vocroz::transport::detail {

/// value を out の先頭から sizeof(T) バイトのビッグエンディアンで書く。
/// 前提条件: out.size() >= sizeof(T)。
template <typename T>
void store_big_endian(T value, std::span<std::uint8_t> out) noexcept {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto shift = (sizeof(T) - 1 - i) * 8;
        out[i] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

/// in の先頭から sizeof(T) バイトをビッグエンディアンとして読む。
/// 前提条件: in.size() >= sizeof(T)。
template <typename T>
[[nodiscard]] T load_big_endian(std::span<const std::uint8_t> in) noexcept {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>((value << 8) | static_cast<T>(in[i]));
    }
    return value;
}

}  // namespace vocroz::transport::detail
