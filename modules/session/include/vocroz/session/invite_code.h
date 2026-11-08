// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待コードの値型と、招待コード・招待 URL・vocroz:// の引数のパース。
// 出演者が貼り付けた文字列や、ブラウザから渡された URL は信頼できない外部入力なので、
// ここで形式を検証し、内側には正規化済みの InviteCode だけを渡す（docs/adr/0006-invite-url-scheme.md）。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "vocroz/common/result.h"

namespace vocroz::session {

/// 招待コードの文字数（区切りのハイフンを除く）。ランデブーサーバーの定義と同じ
/// （infra/rendezvous/internal/invite/code.go）。
inline constexpr std::size_t kInviteCodeLength = 10;

/// 招待の文字列が正しくない理由。
enum class InviteParseError {
    kInvalidFormat,  ///< 招待コードとしても、招待 URL としても読めない
};

/// 正規化済みの招待コード（Crockford Base32 の大文字 10 文字、ハイフンなし）。
/// parse_invite() か parse_invite_code() を通した値だけが入る。
class InviteCode {
public:
    /// 正規化した形（例: "ABCDE12345"）を返す。ランデブーサーバーへ送るときに使う。
    [[nodiscard]] const std::string& normalized() const noexcept {
        return value_;
    }

    /// 人に見せる形（例: "ABCDE-12345"）を返す。
    [[nodiscard]] std::string display() const;

    friend bool operator==(const InviteCode&, const InviteCode&) = default;

private:
    friend common::Result<InviteCode, InviteParseError> parse_invite_code(std::string_view text);

    explicit InviteCode(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

/// 人が入力した招待コードを正規化して検証する。
/// 大文字小文字、ハイフン、空白の違いを無視し、O を 0、I と L を 1 として読む（Crockford Base32
/// の規則。ランデブーサーバーの ParseCode と同じ）。
/// @param text 招待コードの文字列（例: "abcde-12345"）
/// @return 正規化した招待コード。形式が正しくなければ kInvalidFormat
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<InviteCode, InviteParseError> parse_invite_code(std::string_view text);

/// 出演者が貼り付けた招待か、URL スキームで渡された引数を招待コードに変換する。
/// 受け付ける形は次の 3 つ。
/// - 招待コード（例: "ABCDE-12345"）
/// - 招待 URL（例: "https://vocroz.example/join#ABCDE-12345"）
/// - URL スキーム（例: "vocroz://join?code=ABCDE-12345"）。join 以外の操作は受け付けず、
///   code 以外のクエリは無視する
/// 前後の空白は無視する。
/// @param text 招待の文字列
/// @return 正規化した招待コード。どの形でもなければ kInvalidFormat
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<InviteCode, InviteParseError> parse_invite(std::string_view text);

/// 招待 URL（https://<ランデブーサーバー>/join#<表示用の招待コード>）を組み立てる。
/// @param base_url ランデブーサーバーの URL（例: "https://vocroz.example"）。末尾の / は無視する
/// @param code     招待コード
/// @return 招待 URL
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string make_invite_url(std::string_view base_url, const InviteCode& code);

}  // namespace vocroz::session
