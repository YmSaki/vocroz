// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待 API の結果を受け取るリスナー。HttpsRendezvousClient が作業用のスレッドから呼ぶ。
// session の HostSession と ClientSession はコントロールスレッドで結果を受け取るので、
// コンポジションルートがこのリスナーを実装し、コントロールスレッドへ渡してから
// HostSession::on_invite_registered() と ClientSession::on_invite_resolved() を呼ぶ。
#pragma once

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/session/invite_code.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::network {

/// 招待の登録と解決の結果の通知。失効には結果がない（session::RendezvousClient と同じ）。
/// スレッド安全性: HttpsRendezvousClient の作業用のスレッドから呼ばれる。実装はブロックしないこと。
class InviteResultListener {
public:
    virtual ~InviteResultListener() = default;

    /// 招待の登録が終わった。
    /// @param token  登録を要求した招待トークン（session が古い要求の結果を見分けるため）
    /// @param result 登録した招待。失敗の原因は session::RendezvousClient の規則のとおり
    virtual void on_invite_registered(
        const session::InviteToken& token,
        const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>& result) = 0;

    /// 招待コードの解決が終わった。
    /// @param code   解決を要求した招待コード
    /// @param result 解決の結果。失敗の原因は session::RendezvousClient の規則のとおり
    virtual void on_invite_resolved(
        const session::InviteCode& code,
        const common::Result<session::ResolvedInvite, control_protocol::ErrorCode>& result) = 0;
};

}  // namespace vocroz::network
