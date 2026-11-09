// SPDX-License-Identifier: AGPL-3.0-only
//
// crypto_secure_random.h の実装。

#include "crypto_secure_random.h"

#include <cstddef>

#include "vocroz/transport/noise_ik_handshake.h"

namespace vocroz::engine {

bool CryptoSecureRandom::is_available() noexcept {
    // 接続 ID の生成は、libsodium を初期化できなければ kCryptoUnavailable を返す。
    // 同じ乱数の取得元を使うので、これが成功すれば CryptoRandomSource も正しく動く。
    return transport::generate_connection_id().has_value();
}

void CryptoSecureRandom::fill(std::span<std::uint8_t> out) noexcept {
    // 32 ビットずつ取り出して下位のバイトから詰める。
    constexpr std::size_t kBytesPerWord = 4;
    constexpr unsigned kBitsPerByte = 8;
    for (std::size_t i = 0; i < out.size(); i += kBytesPerWord) {
        std::uint32_t word = source_.next_uint32();
        for (std::size_t j = i; j < out.size() && j < i + kBytesPerWord; ++j) {
            out[j] = static_cast<std::uint8_t>(word & 0xFFU);
            word >>= kBitsPerByte;
        }
    }
}

}  // namespace vocroz::engine
