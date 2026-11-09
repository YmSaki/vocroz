// SPDX-License-Identifier: AGPL-3.0-only
//
// RendezvousIdTap の実装。

#include "rendezvous_id_tap.h"

#include <utility>

namespace vocroz::engine {

RendezvousIdTap::RendezvousIdTap(session::RendezvousClient& inner, IdHandler on_id)
    : inner_(inner), on_id_(std::move(on_id)) {}

void RendezvousIdTap::register_invite(const session::InviteRegistration& registration) {
    // 子機は招待を解決するとすぐに UDP で親機を探すので、HTTPS の登録より先に UDP の登録を始める。
    on_id_(registration.rendezvous_id);
    inner_.register_invite(registration);
}

void RendezvousIdTap::revoke_invite(const session::InviteCode& code,
                                    const session::RevokeKey& revoke_key) {
    inner_.revoke_invite(code, revoke_key);
}

void RendezvousIdTap::resolve_invite(const session::InviteCode& code) {
    inner_.resolve_invite(code);
}

}  // namespace vocroz::engine
