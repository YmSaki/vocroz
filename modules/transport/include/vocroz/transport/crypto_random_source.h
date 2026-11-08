// SPDX-License-Identifier: AGPL-3.0-only
//
// 暗号論的な乱数（libsodium）による RandomSource。本番のコンポジションルートで使う。
// 再接続のジッターに暗号論的な強さは要らないが、transport はすでに libsodium を初期化して使うので、
// 別の乱数生成器を持ち込まずに済む。
#pragma once

#include <cstdint>

#include "vocroz/transport/ports/random_source.h"

namespace vocroz::transport {

/// libsodium の randombytes による乱数。
/// スレッド安全性: どのスレッドからも同時に呼び出し可能。
class CryptoRandomSource final : public RandomSource {
public:
    /// 一様な乱数を返す。libsodium の初期化に失敗している環境では 0 を返す
    /// （ジッターが最小になるだけで、再接続の動作は変わらない）。
    [[nodiscard]] std::uint32_t next_uint32() noexcept override;
};

}  // namespace vocroz::transport
