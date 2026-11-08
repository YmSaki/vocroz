// SPDX-License-Identifier: AGPL-3.0-only
//
// update モジュール内の複数のパーサー（バージョン、マニフェスト、ポインターファイル）が
// 共通に使う文字列の小さな処理。外部入力を同じ規則で読むために 1 か所にまとめる。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vocroz::update::detail {

/// 区切り文字で分割する。空の要素も残す（"a..b" は 3 要素）。
[[nodiscard]] std::vector<std::string_view> split(std::string_view text, char delimiter);

/// 空でなく、すべての文字が ASCII の数字なら true。
[[nodiscard]] bool is_all_digits(std::string_view text) noexcept;

/// 小文字の 16 進数の文字列をバイト列に変換する。
/// 大文字を拒否するのは、署名対象のテキストで同じ値に複数の表記を許さないため。
/// @return 長さが out の 2 倍で、すべて小文字の 16 進数なら true。
[[nodiscard]] bool decode_lower_hex(std::string_view text, std::span<std::uint8_t> out) noexcept;

/// バイト列を小文字の 16 進数にする。
[[nodiscard]] std::string encode_lower_hex(std::span<const std::uint8_t> bytes);

/// "key=value" の 1 行を、期待するキーで読む。
/// @return キーが一致すれば値の部分。一致しなければ std::nullopt。
[[nodiscard]] std::optional<std::string_view> value_of(std::string_view line,
                                                       std::string_view key) noexcept;

}  // namespace vocroz::update::detail
