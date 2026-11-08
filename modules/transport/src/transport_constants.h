// SPDX-License-Identifier: AGPL-3.0-only
//
// transport の内部だけで使う定数。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vocroz::transport {

/// ワイヤーフォーマットのバージョン。形式を変えるときは値を上げ、古い値のパケットは捨てる。
inline constexpr std::uint8_t kProtocolVersion = 1;

/// 使う Noise プロトコル（docs/adr/0001-key-exchange-noise-ik.md）。
inline constexpr const char* kNoiseProtocolName = "Noise_IK_25519_ChaChaPoly_BLAKE2s";

/// Noise のプロローグの先頭に置くラベル。後ろにバージョンと接続 ID を続ける。
/// プロローグはハンドシェイクのハッシュに混ぜられるので、別の製品やバージョンとの取り違えと、
/// 平文のヘッダーにある接続 ID の書き換えを、ハンドシェイクの失敗として検出できる。
inline constexpr std::string_view kPrologueLabel = "VOCROZ transport";

/// ChaCha20-Poly1305（IETF）の nonce の長さ（バイト）。
inline constexpr std::size_t kAeadNonceBytes = 12;

/// Noise のハンドシェイクメッセージの一時公開鍵の長さ（バイト）。
inline constexpr std::size_t kEphemeralKeyBytes = 32;

}  // namespace vocroz::transport
