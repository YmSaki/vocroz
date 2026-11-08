// SPDX-License-Identifier: AGPL-3.0-only
//
// transport が使う暗号ライブラリ（libsodium、noise-c）の初期化。
// どちらも使う前に 1 回の初期化が要るので、鍵の生成とハンドシェイクの入口で呼ぶ。
#pragma once

namespace vocroz::transport::detail {

/// libsodium と noise-c を初期化する。2 回目以降は最初の結果を返すだけで、何もしない。
/// @return 初期化に成功していれば true
/// スレッド安全性: どのスレッドからも呼び出し可能（関数内の static の初期化で 1 回だけ実行する）。
[[nodiscard]] bool ensure_crypto_initialized() noexcept;

}  // namespace vocroz::transport::detail
