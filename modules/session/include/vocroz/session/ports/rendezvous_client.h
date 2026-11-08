// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの招待 API（登録、解決、失効）を呼ぶポート。network が HTTPS で実装する
// （形式は infra/rendezvous/internal/httpapi/invites.go）。
// 通信はブロックするので、実装は別スレッドで行い、結果をコントロールスレッドで
// HostSession::on_invite_registered() か ClientSession::on_invite_resolved() に渡す。
#pragma once

#include "vocroz/session/identifiers.h"
#include "vocroz/session/invite_code.h"
#include "vocroz/session/ports/host_link.h"

namespace vocroz::session {

/// 招待の登録の要求。親機が作った値をランデブーサーバーに預ける。
struct InviteRegistration {
    HostPublicKey host_public_key{};
    InviteToken invite_token{};
    RendezvousId rendezvous_id{};

    friend bool operator==(const InviteRegistration&, const InviteRegistration&) = default;
};

/// 登録の結果。
struct RegisteredInvite {
    InviteCode code;
    /// 失効に使う鍵。親機のメモリにだけ置き、ログに出さない。
    RevokeKey revoke_key{};

    friend bool operator==(const RegisteredInvite&, const RegisteredInvite&) = default;
};

/// 解決の結果。子機が親機へ接続し、参加を申請するのに使う。
struct ResolvedInvite {
    HostEndpoint endpoint;
    InviteToken invite_token{};

    friend bool operator==(const ResolvedInvite&, const ResolvedInvite&) = default;
};

/// ランデブーサーバーの招待 API。
/// 失敗の理由は control_protocol::ErrorCode で返す。期限切れは kInviteExpired、取り消し済みは
/// kInviteRevoked、存在しないコードは kInviteInvalid、サーバーに届かないときは kUnreachable。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックしない。
class RendezvousClient {
public:
    virtual ~RendezvousClient() = default;

    /// 招待を登録する。結果は HostSession::on_invite_registered() で返す。
    /// @param registration 登録する値
    virtual void register_invite(const InviteRegistration& registration) = 0;

    /// 招待を失効させる。結果は返さない（失敗しても、親機は失効した招待での参加を拒否する）。
    /// @param code       失効させる招待コード
    /// @param revoke_key 登録時に受け取った鍵
    virtual void revoke_invite(const InviteCode& code, const RevokeKey& revoke_key) = 0;

    /// 招待コードを解決する。結果は ClientSession::on_invite_resolved() で返す。
    /// @param code 解決する招待コード
    virtual void resolve_invite(const InviteCode& code) = 0;
};

}  // namespace vocroz::session
