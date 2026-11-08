// SPDX-License-Identifier: AGPL-3.0-only
//
// FaultProfile に従って、1 つのパケットをいつ、何個、どう壊して届けるかを決める。
// 時刻もソケットも持たない純粋な判定にして、シミュレーションのネットワーク（SimulatedNetwork）と
// 実ソケットの UDP 障害注入プロキシの両方から同じ規則で使えるようにする。
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/netsim/deterministic_random.h"
#include "vocroz/netsim/fault_profile.h"

namespace vocroz::netsim {

/// 1 つのパケットの 1 回の配送。
struct Delivery {
    /// 送信から到着までの時間。
    std::chrono::microseconds delay{0};
    /// 1 ビットを反転させるなら true。
    bool corrupt = false;
    /// 反転させるビットを選ぶ値。corrupt_packet() に渡す。
    std::uint64_t corrupt_selector = 0;
};

/// 1 つのパケットの配送の決定。欠損なら count が 0、重複なら 2。
struct FaultDecision {
    /// 配送。先頭 count 個だけが有効。
    std::array<Delivery, 2> deliveries{};
    /// 配送の数（0〜2）。
    std::size_t count = 0;
    /// 欠損させたなら true。
    bool lost = false;
    /// 重複させたなら true。
    bool duplicated = false;
    /// 順序を入れ替えるために余分に遅らせたなら true。
    bool reordered = false;
};

/// パケットごとの障害の判定。
/// 判定の順序（欠損 → 遅延と揺らぎ → 順序の入れ替え → 重複 → 改ざん）と、1
/// パケットで消費する乱数の数を 固定し、同じシードと同じ送信の数なら同じ決定の列になるようにする。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
class FaultInjector {
public:
    /// @param profile 障害の設定
    explicit FaultInjector(const FaultProfile& profile) noexcept;

    /// 次のパケットの配送を決める。
    /// @return 配送の決定
    [[nodiscard]] FaultDecision decide() noexcept;

    /// 設定を返す。
    [[nodiscard]] const FaultProfile& profile() const noexcept {
        return profile_;
    }

private:
    FaultProfile profile_;
    DeterministicRandom random_;
};

/// Delivery::corrupt_selector が選ぶ 1 ビットを反転させる。
/// @param packet   書き換えるパケット。空なら何もしない
/// @param selector Delivery::corrupt_selector
void corrupt_packet(std::span<std::uint8_t> packet, std::uint64_t selector) noexcept;

}  // namespace vocroz::netsim
