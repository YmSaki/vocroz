// SPDX-License-Identifier: AGPL-3.0-only
//
// base64url（RFC 4648 の 5 節、パディングなし）の符号化と復号。
// ランデブーサーバーの招待 API は、鍵やトークンなどのバイト列をこの形で JSON に載せる
// （infra/rendezvous/internal/httpapi/invites.go の RawURLEncoding）。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vocroz::network::wire {

/// バイト列を base64url（パディングなし）で符号化する。
/// @param bytes 符号化するバイト列
/// @return 符号化した文字列
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string encode_base64url(std::span<const std::uint8_t> bytes);

/// base64url（パディングなし）の文字列を復号する。
/// サーバーの Go の RawURLEncoding と同じく、パディング、空白、標準の base64 の文字（+ と /）は
/// 受け付けない。加えて、末尾の余りのビットが 0 でない文字列も受け付けない（サーバーは常に 0
/// で符号化するので、そうでなければ壊れた応答とみなす）。
/// @param text 復号する文字列
/// @return 復号したバイト列。形式が正しくなければ std::nullopt
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::optional<std::vector<std::uint8_t>> decode_base64url(std::string_view text);

}  // namespace vocroz::network::wire
