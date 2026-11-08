// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機から届く表示名など、外部から来る文字列の検査。
// UI へは JSON で送るので、不正な UTF-8
// を内側に入れない（エンコーダーが置換や失敗をしないように）。
#pragma once

#include <cstddef>
#include <string_view>

namespace vocroz::session::detail {

/// 表示してよい文字列なら true を返す。
/// 正しい UTF-8 で、max_bytes 以下で、制御文字（U+0000〜U+001F、U+007F〜U+009F）を含まないこと。
/// 制御文字を拒むのは、改行などで画面やログの表示を崩されないようにするため。
/// @param text      検査する文字列
/// @param max_bytes 上限のバイト数
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] bool is_displayable_text(std::string_view text, std::size_t max_bytes) noexcept;

}  // namespace vocroz::session::detail
