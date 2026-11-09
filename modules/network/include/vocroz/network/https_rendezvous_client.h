// SPDX-License-Identifier: AGPL-3.0-only
//
// session の RendezvousClient を、ランデブーサーバーの招待 API（HTTPS）で実装する。
// HTTPS の往復は数百ミリ秒から数秒かかるので、コントロールスレッドを止めないよう、
// 要求をキューに積んで作業用のスレッドで 1 つずつ送り、結果を InviteResultListener に渡す。
#pragma once

#include <memory>

#include "vocroz/network/ports/https_transport.h"
#include "vocroz/network/ports/invite_result_listener.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::network {

/// 招待 API のクライアント。
/// スレッド安全性: register_invite()、revoke_invite()、resolve_invite() はコントロールスレッドから
/// 呼ぶ（キューに積むだけでブロックしない）。transport と listener は作業用のスレッドから使う。
class HttpsRendezvousClient final : public session::RendezvousClient {
public:
    /// 作業用のスレッドを起動する。
    /// @param transport 招待 API への POST。このオブジェクトより長く生きること
    /// @param listener  結果の渡し先。このオブジェクトより長く生きること
    HttpsRendezvousClient(HttpsTransport& transport, InviteResultListener& listener);

    /// キューに残った要求を送り終えてから、作業用のスレッドを止めて待つ。
    /// セッションの終了時の失効を送り漏らさないようにするため。サーバーに届かなければ、
    /// 要求 1 つにつき最大で HttpsTransport のタイムアウトの分だけ待つ。
    ~HttpsRendezvousClient() override;

    HttpsRendezvousClient(const HttpsRendezvousClient&) = delete;
    HttpsRendezvousClient& operator=(const HttpsRendezvousClient&) = delete;

    void register_invite(const session::InviteRegistration& registration) override;
    void revoke_invite(const session::InviteCode& code,
                       const session::RevokeKey& revoke_key) override;
    void resolve_invite(const session::InviteCode& code) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::network
