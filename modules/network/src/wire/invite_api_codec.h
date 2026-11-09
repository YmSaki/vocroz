// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの招待 API（登録、解決、失効）の JSON の組み立てと、応答の検証。
// スキーマの正本は infra/rendezvous/internal/httpapi/invites.go で、ここはそのクライアント側
// （AGENTS.md 4.4）。応答は信頼できない外部入力なので、ここで形式と長さを検証し、
// session には検証済みの値と原因別のエラーコードだけを渡す。
#pragma once

#include <string>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/network/ports/https_transport.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/session/invite_code.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::network::wire {

/// POST /v1/invites の本文を組み立てる。有効期限は送らず、サーバーの上限（30 分）に任せる。
/// @param registration 登録する値
/// @return JSON の本文
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string encode_register_request(const session::InviteRegistration& registration);

/// POST /v1/invites/resolve の本文を組み立てる。
/// @param code 解決する招待コード
/// @return JSON の本文
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string encode_resolve_request(const session::InviteCode& code);

/// POST /v1/invites/revoke の本文を組み立てる。
/// @param code       失効させる招待コード
/// @param revoke_key 登録時に受け取った鍵
/// @return JSON の本文
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string encode_revoke_request(const session::InviteCode& code,
                                                const session::RevokeKey& revoke_key);

/// POST /v1/invites の応答を検証する。
/// @param response サーバーの応答
/// @return 登録した招待。サーバーが上限に達していたり 5xx を返したりしたら kUnreachable、
///         要求が正しくないと言われたら kEngineError（クライアントの不具合）、
///         成功の応答の形式が正しくなければ kUnreachable
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<session::RegisteredInvite, control_protocol::ErrorCode>
decode_register_response(const HttpResponse& response);

/// POST /v1/invites/resolve の応答を検証する。
/// @param requested 解決を要求した招待コード。応答のコードがこれと違えば壊れた応答とみなす
/// @param response  サーバーの応答
/// @return 親機への接続に使う値。存在しないか形式の正しくないコードなら kInviteInvalid、
///         期限切れなら kInviteExpired、取り消し済みなら kInviteRevoked、5xx なら kUnreachable、
///         成功の応答の形式が正しくなければ kUnreachable
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<session::ResolvedInvite, control_protocol::ErrorCode>
decode_resolve_response(const session::InviteCode& requested, const HttpResponse& response);

}  // namespace vocroz::network::wire
