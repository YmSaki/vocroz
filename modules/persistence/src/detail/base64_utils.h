// SPDX-License-Identifier: AGPL-3.0-only
//
// Base64（RFC 4648 の 4 節、パディングあり）の符号化と復号。
// 設定のファイルにバイト列（プラグインの状態）を JSON の文字列として載せるために使う。
// ランデブーサーバーの招待 API の base64url（network）とは、形式もサーバーに合わせる理由もないので
// 別にする。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vocroz::persistence::detail {

/// バイト列を Base64（パディングあり）で符号化する。
/// @param bytes 符号化するバイト列
/// @return 符号化した文字列
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string encode_base64(std::span<const std::uint8_t> bytes);

/// Base64（パディングあり）の文字列を復号する。
/// 長さが 4 の倍数でないもの、空白、パディングの位置の誤り、末尾の余りのビットが 0 でないものは
/// 受け付けない。自分で書いたファイルを読むだけなので、ほかの書き方は壊れたファイルとみなす。
/// @param text 復号する文字列
/// @return 復号したバイト列。形式が正しくなければ std::nullopt
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::optional<std::vector<std::uint8_t>> decode_base64(std::string_view text);

}  // namespace vocroz::persistence::detail
