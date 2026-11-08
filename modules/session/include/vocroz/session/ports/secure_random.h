// SPDX-License-Identifier: AGPL-3.0-only
//
// 暗号論的に安全な乱数のポート。招待トークン、再接続トークン、ランデブー ID の生成に使う。
// 乱数の生成を差し替えられるようにして、テストを決定的にする（AGENTS.md 1.2 のテスタビリティ）。
// 実装はコンポジションルートで、libsodium を使う transport の乱数に委ねる（暗号プリミティブを自作しない）。
#pragma once

#include <cstdint>
#include <span>

namespace vocroz::session {

/// 推測できない乱数の取得元。
class SecureRandom {
public:
    virtual ~SecureRandom() = default;

    /// out を一様な乱数のバイトで埋める。失敗しない（失敗する実装はプロセスを止める）。
    /// @param out 埋める領域
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    virtual void fill(std::span<std::uint8_t> out) noexcept = 0;
};

}  // namespace vocroz::session
