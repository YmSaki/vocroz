// SPDX-License-Identifier: AGPL-3.0-only
//
// 障害注入に使う、シードから決定的に定まる乱数列。
// std::mt19937 と std::uniform_real_distribution
// の組み合わせは、分布の実装が標準ライブラリごとに違い、 同じシードでも MSVC と libstdc++
// で結果が変わる。Windows の実機と Linux の CI で同じ障害の列を
// 再現できるように、生成器（SplitMix64）と [0, 1) への変換をここで固定する。
#pragma once

#include <cstdint>

namespace vocroz::netsim {

/// SplitMix64 の乱数生成器。
/// 暗号用途には使わない（障害の再現だけが目的）。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
class DeterministicRandom {
public:
    /// @param seed シード。同じシードからは、どの環境でも同じ列が出る
    explicit constexpr DeterministicRandom(std::uint64_t seed) noexcept : state_(seed) {}

    /// 次の 64 ビットの値を返す。
    [[nodiscard]] constexpr std::uint64_t next_u64() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    /// [0, 1) の一様な値を返す。
    /// 上位 53 ビットを使い、double の仮数部に収まる範囲で等間隔にする。
    [[nodiscard]] constexpr double next_unit() noexcept {
        constexpr double kScale = 1.0 / static_cast<double>(1ULL << 53U);
        return static_cast<double>(next_u64() >> 11U) * kScale;
    }

    /// 確率 probability で true を返す。
    /// @param probability 0 以下なら常に false、1 以上なら常に true
    [[nodiscard]] constexpr bool chance(double probability) noexcept {
        // 確率 0 のときも乱数を 1
        // つ消費して、他の障害の設定を変えても列の対応がずれないようにする。
        return next_unit() < probability;
    }

    /// [0, bound) の値を返す。bound が 0 なら 0。
    [[nodiscard]] constexpr std::uint64_t below(std::uint64_t bound) noexcept {
        const std::uint64_t value = next_u64();
        return bound == 0 ? 0 : value % bound;
    }

private:
    std::uint64_t state_;
};

}  // namespace vocroz::netsim
