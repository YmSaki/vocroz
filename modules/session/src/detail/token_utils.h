// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待トークンと再接続トークンの生成と照合。
// どちらも子機が提示する秘密の値なので、照合にかかる時間から一致した長さを推測されないよう、
// 内容によらず同じ手順で比べる。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "vocroz/session/ports/secure_random.h"

namespace vocroz::session::detail {

/// 2 つの固定長のバイト列が等しければ true を返す。途中で打ち切らず、常に全バイトを比べる。
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
template <std::size_t N>
[[nodiscard]] bool constant_time_equal(const std::array<std::uint8_t, N>& a,
                                       const std::array<std::uint8_t, N>& b) noexcept {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < N; ++i) {
        diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    }
    return diff == 0;
}

/// 乱数で埋めた固定長のバイト列を作る。
/// @param random 乱数の取得元
/// スレッド安全性: random と同じ。
template <std::size_t N>
[[nodiscard]] std::array<std::uint8_t, N> random_bytes(SecureRandom& random) noexcept {
    std::array<std::uint8_t, N> out{};
    random.fill(out);
    return out;
}

}  // namespace vocroz::session::detail
