// SPDX-License-Identifier: AGPL-3.0-only
//
// Noise_IK の静的鍵（Curve25519 の鍵ペア）。
// 親機の公開鍵は招待トークンに入り、子機は接続前に親機の公開鍵を知っている
// （docs/adr/0001-key-exchange-noise-ik.md）。秘密鍵はメモリ上でも寿命を短くするため、
// 破棄時にゼロで上書きする。
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// Curve25519 の公開鍵。
using PublicKey = std::array<std::uint8_t, kPublicKeyBytes>;

/// Curve25519 の鍵ペア。コピーできず、ムーブだけできる（秘密鍵の複製を作らないため）。
/// スレッド安全性: 通常の値型と同じ。生成と破棄はコントロールスレッドで行う。
class StaticKeyPair {
public:
    /// 乱数で新しい鍵ペアを生成する。乱数は libsodium の CSPRNG を使う。
    /// @return 生成した鍵ペア。暗号ライブラリを初期化できなければ kCryptoUnavailable
    [[nodiscard]] static common::Result<StaticKeyPair, TransportError> generate() noexcept;

    StaticKeyPair(const StaticKeyPair&) = delete;
    StaticKeyPair& operator=(const StaticKeyPair&) = delete;
    /// ムーブ元の秘密鍵はゼロで上書きする。
    StaticKeyPair(StaticKeyPair&& other) noexcept;
    /// ムーブ元の秘密鍵はゼロで上書きする。
    StaticKeyPair& operator=(StaticKeyPair&& other) noexcept;
    /// 秘密鍵をゼロで上書きしてから破棄する。
    ~StaticKeyPair();

    /// 公開鍵を返す。
    [[nodiscard]] const PublicKey& public_key() const noexcept {
        return public_key_;
    }

    /// 秘密鍵を返す。ハンドシェイクに渡すためだけに使い、ログや設定ファイルに平文で書かない
    /// （AGENTS.md 6.4）。
    [[nodiscard]] std::span<const std::uint8_t, kPrivateKeyBytes> private_key() const noexcept {
        return private_key_;
    }

private:
    StaticKeyPair() = default;

    PublicKey public_key_{};
    std::array<std::uint8_t, kPrivateKeyBytes> private_key_{};
};

}  // namespace vocroz::transport
