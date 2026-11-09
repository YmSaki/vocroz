// SPDX-License-Identifier: AGPL-3.0-only
//
// session の SecureRandom の実装。transport の CryptoRandomSource（libsodium の CSPRNG）に委ね、
// 暗号論的な乱数を自作しない（AGENTS.md 6.4、docs/adr/0022-session-domain.md）。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/session/ports/secure_random.h"
#include "vocroz/transport/crypto_random_source.h"

namespace vocroz::engine {

/// libsodium による SecureRandom。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class CryptoSecureRandom final : public session::SecureRandom {
public:
    /// libsodium を初期化できるかを確かめる。CryptoRandomSource は初期化に失敗すると 0 を返し続け、
    /// トークンが推測できる値になるので、起動時にこれが false ならエンジンを起動しない。
    /// @return 暗号論的な乱数を使えるなら true
    [[nodiscard]] static bool is_available() noexcept;

    /// out を乱数で埋める。is_available() が true の環境でだけ使う。
    void fill(std::span<std::uint8_t> out) noexcept override;

private:
    transport::CryptoRandomSource source_;
};

}  // namespace vocroz::engine
