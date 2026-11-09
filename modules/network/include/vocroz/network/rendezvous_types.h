// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーとのやり取りで使う識別子と役割の型。
// 値の長さと役割の番号は infra/rendezvous/internal/udpapi/wire.go と同じにする（AGENTS.md 4.4）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vocroz::network {

/// ランデブー ID とピア ID の長さ（バイト）。
inline constexpr std::size_t kRendezvousIdBytes = 16;

/// 親機と子機が落ち合うための ID。親機がセッションの作成時に乱数で決め、招待で子機に渡る。
using RendezvousId = std::array<std::uint8_t, kRendezvousIdBytes>;

/// ランデブーサーバーで登録者を区別する ID。親機はセッションごと、子機は試行ごとに乱数で決める。
using PeerId = std::array<std::uint8_t, kRendezvousIdBytes>;

/// 1 回の登録で申告できるローカル候補アドレスの上限。サーバーの MaxLocalCandidates と同じ値。
inline constexpr std::size_t kMaxLocalCandidates = 8;

/// 登録者の役割。値はワイヤー上の 1 バイトにそのまま載る。
enum class RendezvousRole : std::uint8_t {
    /// 親機（pa-engine）。
    kHost = 1,
    /// 子機。
    kClient = 2,
};

}  // namespace vocroz::network
