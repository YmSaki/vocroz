// SPDX-License-Identifier: AGPL-3.0-only
//
// session の RendezvousClient を包み、招待の登録の要求に載ったランデブー ID を知らせる。
// ランデブー ID は session がセッションの作成時に乱数で決め、招待の登録（HTTPS）と
// 親機の UDP の登録（HostRendezvous）の両方に使う。session のポートを増やさずに、
// UDP の登録を同じ ID で始めるため（docs/adr/0028-engine-assembly.md）。
#pragma once

#include <functional>

#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::engine {

/// ランデブー ID を知らせる RendezvousClient。
/// スレッド安全性: コントロールスレッドから呼ぶ（session の約束のとおり）。
class RendezvousIdTap final : public session::RendezvousClient {
public:
    /// 登録の要求に載ったランデブー ID の受け取り手。
    using IdHandler = std::function<void(const session::RendezvousId&)>;

    /// @param inner      要求をそのまま渡す先。このオブジェクトより長く生存させる
    /// @param on_id      ランデブー ID の受け取り手。要求を渡す前に呼ぶ
    RendezvousIdTap(session::RendezvousClient& inner, IdHandler on_id);

    void register_invite(const session::InviteRegistration& registration) override;
    void revoke_invite(const session::InviteCode& code,
                       const session::RevokeKey& revoke_key) override;
    void resolve_invite(const session::InviteCode& code) override;

private:
    session::RendezvousClient& inner_;
    IdHandler on_id_;
};

}  // namespace vocroz::engine
