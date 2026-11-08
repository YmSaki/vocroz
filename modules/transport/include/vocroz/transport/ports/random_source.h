// SPDX-License-Identifier: AGPL-3.0-only
//
// 乱数の取得元のポート。再接続のジッターなど、乱数を使う処理をテストで決定的に動かすために、
// 乱数の取得をこのポートに集約する（AGENTS.md 1.2 のテスタビリティ）。
#pragma once

#include <cstdint>

namespace vocroz::transport {

/// 一様な 32 ビットの乱数の取得元。
class RandomSource {
public:
    virtual ~RandomSource() = default;

    /// 0 以上 2^32 - 1 以下の一様な乱数を返す。
    /// スレッド安全性: 実装ごとに異なる。CryptoRandomSource はどのスレッドからも呼び出し可能。
    [[nodiscard]] virtual std::uint32_t next_uint32() noexcept = 0;
};

}  // namespace vocroz::transport
