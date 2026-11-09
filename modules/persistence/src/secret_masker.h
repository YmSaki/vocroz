// SPDX-License-Identifier: AGPL-3.0-only
//
// ログと診断レポートの文字列から、秘密情報と個人情報に当たる部分を伏せる。
// ログに秘密情報を書かない規則（AGENTS.md 6.4）は書く側で守るが、守れなかったときに
// ファイルやサポートへの送付で漏れないよう、出力の直前でもう一度伏せる（多層防御）。
// ログファイルへの出力と診断レポートの両方が同じ規則を使うので、規則はここ 1 か所に置く。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::persistence {

/// text の中の次の部分を伏せた文字列を返す。
/// - 長い英数字の並び（32 文字以上で数字を含む。16 進数や base64url の鍵、トークン）→ `<secret>`
/// - 招待コード（Crockford Base32 の大文字 10 文字か、5 文字 + ハイフン + 5 文字）→ `<invite-code>`
/// - プライベートでない IPv4 アドレス → `<ip>`。
///   LAN やループバックのアドレスは接続の調査に要るので残す
/// - ユーザーのプロファイルのフォルダ名（`C:\Users\<名前>\`）→ `<user>`
/// 表示名やセッション名のような自由な文字列は形から見分けられないので、ここでは伏せない。
/// 書く側でログに入れないことと、診断レポートでは値ごと伏せることで守る。
/// @param text 伏せる前の文字列
/// @return 伏せた後の文字列
[[nodiscard]] std::string mask_sensitive_text(std::string_view text);

}  // namespace vocroz::persistence
