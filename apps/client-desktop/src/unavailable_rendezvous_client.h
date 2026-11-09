// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの URL がないか使えないときの RendezvousClient。招待の解決をつねに
// UNREACHABLE で返す。ビルドに URL を埋め込まず、--rendezvous-url も渡さずに起動したときや、
// URL の形が誤っているときに使う。参加だけができず、画面は理由を表示できる。
#pragma once

#include "vocroz/network/ports/invite_result_listener.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::client {

/// 招待の解決をつねに UNREACHABLE で返す RendezvousClient。
/// スレッド安全性: コントロールスレッドから呼ぶ。結果は同じスレッドで listener に渡すので、
/// listener（InviteInbox）の生産者はこのスレッドだけになる。
class UnavailableRendezvousClient final : public session::RendezvousClient {
public:
    /// @param listener 結果の渡し先。このオブジェクトより長く生存させる
    explicit UnavailableRendezvousClient(network::InviteResultListener& listener) noexcept;

    /// 子機は登録しないが、呼ばれたら登録も UNREACHABLE で返す。
    void register_invite(const session::InviteRegistration& registration) override;
    /// 失効は結果を返さないので何もしない。
    void revoke_invite(const session::InviteCode& code,
                       const session::RevokeKey& revoke_key) override;
    /// UNREACHABLE を返す。
    void resolve_invite(const session::InviteCode& code) override;

private:
    network::InviteResultListener& listener_;
};

}  // namespace vocroz::client
