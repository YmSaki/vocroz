// SPDX-License-Identifier: AGPL-3.0-only
//
// libsodium の初期化。署名検証とハッシュの両方が、使う前に必ず呼ぶ。
#pragma once

namespace vocroz::update::detail {

/// sodium_init() を呼ぶ。2 回目以降の呼び出しは何もしない（libsodium がスレッド安全に扱う）。
/// @return 初期化に成功していれば true。
[[nodiscard]] bool ensure_sodium_initialized() noexcept;

}  // namespace vocroz::update::detail
