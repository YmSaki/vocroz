// SPDX-License-Identifier: AGPL-3.0-only
//
// セッションで使う識別子と秘密の値の型。
// 鍵とトークンは固定長のバイト列で持ち、長さの誤りを型で防ぐ。
// トークンと鍵はログや設定ファイルに書かない（AGENTS.md 6.4）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vocroz::session {

/// 親機の Noise の静的公開鍵（X25519、32 バイト）。招待に含め、子機が親機を確かめるのに使う。
using HostPublicKey = std::array<std::uint8_t, 32>;

/// 招待トークンのバイト数。仕様の「128 bit 以上のエントロピー」を余裕をもって満たす 256 bit。
/// ランデブーサーバーは 16〜64 バイトを受け付ける（docs/adr/0012-rendezvous-protocol.md）。
inline constexpr std::size_t kInviteTokenBytes = 32;

/// 招待トークン。招待コードを解決した子機だけが知り、参加の申請に添える。
using InviteToken = std::array<std::uint8_t, kInviteTokenBytes>;

/// 再接続トークン。承認時に親機が発行し、セッションの終了かストリップの削除まで有効。
/// 持っている子機は、承認を経ずに同じストリップへ戻れる。推測されないよう 256 bit の乱数にする。
using ReconnectToken = std::array<std::uint8_t, 32>;

/// ランデブー ID。セッションに紐づき、親機と子機がホールパンチの相手を見つけるのに使う
/// （docs/adr/0012-rendezvous-protocol.md）。
using RendezvousId = std::array<std::uint8_t, 16>;

/// 招待の失効に使う鍵。ランデブーサーバーが登録時に返し、親機だけが保持する。
using RevokeKey = std::array<std::uint8_t, 32>;

/// 親機から見た、子機との 1 つの接続（暗号化済みの経路）の識別子。
/// transport とその組み立てが接続ごとに振る。再接続すると別の値になるので、
/// 参加者の識別には使わず、参加者は participant_id と再接続トークンで識別する。
using PeerHandle = std::uint64_t;

}  // namespace vocroz::session
