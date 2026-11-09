// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーに届かないときと同じ結果を返す RendezvousClient。
// 招待の HTTPS（network、N1 の #18 の続きの PR）が入るまでの仮の実装で、セッションの作成は
// UNREACHABLE で失敗する。これはオフラインの会場での振る舞い（セッションの作成と参加だけを
// 不可とし、ミキサーとローカルの入出力は使える。docs/adr/0008-runtime-behavior.md）と同じ。
// N1 のマージ後にコンポジションルートで差し替えて、このファイルは消す。
#pragma once

#include <functional>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/ports/rendezvous_client.h"

#include "control_loop.h"

namespace vocroz::engine {

/// 招待の登録をつねに UNREACHABLE で返す RendezvousClient。
/// スレッド安全性: コントロールスレッドから呼ぶ。結果はコントロールループに積んで返す
/// （session の呼び出しの中から session を呼び返さないため）。
class OfflineRendezvousClient final : public session::RendezvousClient {
public:
    /// 登録の結果を受け取る処理（HostSession::on_invite_registered()）。
    using RegistrationHandler = std::function<void(
        const session::InviteToken&,
        const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>&)>;

    /// @param loop 結果を積むループ。このオブジェクトより長く生存させる
    explicit OfflineRendezvousClient(ControlLoop& loop) noexcept : loop_(loop) {}

    /// 登録の結果の受け取り先を設定する。session を作ったあとで 1 回だけ呼ぶ。
    /// @param handler 受け取る処理
    void set_registration_handler(RegistrationHandler handler) {
        handler_ = std::move(handler);
    }

    void register_invite(const session::InviteRegistration& registration) override;
    void revoke_invite(const session::InviteCode& code,
                       const session::RevokeKey& revoke_key) override;
    void resolve_invite(const session::InviteCode& code) override;

private:
    ControlLoop& loop_;
    RegistrationHandler handler_;
};

}  // namespace vocroz::engine
