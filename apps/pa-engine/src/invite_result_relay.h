// SPDX-License-Identifier: AGPL-3.0-only
//
// network の HttpsRendezvousClient が作業用のスレッドから返す招待の結果を、コントロールスレッドの
// ループに積んで session に渡す。session はコントロールスレッドだけで動くため
// （AGENTS.md 2.3、vocroz/network/ports/invite_result_listener.h）。
#pragma once

#include "vocroz/network/ports/invite_result_listener.h"

#include "control_loop.h"
#include "invite_registration_handler.h"

namespace vocroz::engine {

/// 招待の結果をコントロールスレッドへ渡すリスナー。
/// スレッド安全性: on_invite_registered() と on_invite_resolved() は作業用のスレッドから呼ばれる。
/// set_registration_handler() はコントロールスレッドから、結果が届き始める前に呼ぶ。
class InviteResultRelay final : public network::InviteResultListener {
public:
    /// @param loop 結果を積むループ。このオブジェクトより長く生存させる
    explicit InviteResultRelay(ControlLoop& loop) noexcept : loop_(loop) {}

    /// 登録の結果の受け取り先を設定する。session を作ったあとで 1 回だけ呼ぶ。
    /// @param handler 受け取る処理（コントロールスレッドで呼ばれる）
    void set_registration_handler(InviteRegistrationHandler handler) {
        handler_ = std::move(handler);
    }

    void on_invite_registered(const session::InviteToken& token,
                              const common::Result<session::RegisteredInvite,
                                                   control_protocol::ErrorCode>& result) override;
    void on_invite_resolved(const session::InviteCode& code,
                            const common::Result<session::ResolvedInvite,
                                                 control_protocol::ErrorCode>& result) override;

private:
    ControlLoop& loop_;
    InviteRegistrationHandler handler_;
};

}  // namespace vocroz::engine
