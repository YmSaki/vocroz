// SPDX-License-Identifier: AGPL-3.0-only
//
// transport の外でも使う定数。受信バッファの大きさを決める network や、
// 公開鍵を扱う session が、ワイヤーフォーマットと同じ値を使えるようにする。
#pragma once

#include <cstddef>

namespace vocroz::transport {

/// UDP ペイロードとして送受信するパケットの最大長（バイト）。
/// IPv6 の最小 MTU（1280）から IPv6 ヘッダー（40）と UDP ヘッダー（8）を引いた値で、
/// どの経路でも IP フラグメンテーションを起こさない。フラグメントは 1 つ失うと
/// パケット全体が失われ、遅延と欠損が増えるため避ける。
inline constexpr std::size_t kMaxPacketBytes = 1232;

/// Curve25519 の公開鍵の長さ（バイト）。
inline constexpr std::size_t kPublicKeyBytes = 32;

/// Curve25519 の秘密鍵の長さ（バイト）。
inline constexpr std::size_t kPrivateKeyBytes = 32;

}  // namespace vocroz::transport
