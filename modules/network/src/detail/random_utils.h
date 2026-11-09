// SPDX-License-Identifier: AGPL-3.0-only
//
// 乱数の取得元（transport::RandomSource）から固定長のバイト列を作る。
// ピア ID、トランザクション ID、PCP の nonce に使う。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "vocroz/transport/ports/random_source.h"

namespace vocroz::network::detail {

/// N バイトの乱数のバイト列を返す。
/// @param random 乱数の取得元
template <std::size_t N>
[[nodiscard]] std::array<std::uint8_t, N> random_bytes(transport::RandomSource& random) noexcept {
    std::array<std::uint8_t, N> out{};
    std::uint32_t word = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (i % 4 == 0) {
            word = random.next_uint32();
        }
        out[i] = static_cast<std::uint8_t>(word >> (8 * (i % 4)));
    }
    return out;
}

}  // namespace vocroz::network::detail
