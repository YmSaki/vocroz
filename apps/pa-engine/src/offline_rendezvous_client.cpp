// SPDX-License-Identifier: AGPL-3.0-only
//
// offline_rendezvous_client.h の実装。

#include "offline_rendezvous_client.h"

namespace vocroz::engine {

void OfflineRendezvousClient::register_invite(const session::InviteRegistration& registration) {
    loop_.post([this, token = registration.invite_token] {
        if (handler_) {
            handler_(token, common::make_unexpected(control_protocol::ErrorCode::kUnreachable));
        }
    });
}

void OfflineRendezvousClient::revoke_invite(const session::InviteCode& /*code*/,
                                            const session::RevokeKey& /*revoke_key*/) {
    // 登録できた招待がないので、失効させるものもない。
}

void OfflineRendezvousClient::resolve_invite(const session::InviteCode& /*code*/) {
    // 親機は招待を解決しない（子機だけが使う）。
}

}  // namespace vocroz::engine
