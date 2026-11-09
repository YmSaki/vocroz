// SPDX-License-Identifier: AGPL-3.0-only
//
// UnavailableRendezvousClient の実装。

#include "unavailable_rendezvous_client.h"

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::client {

UnavailableRendezvousClient::UnavailableRendezvousClient(
    network::InviteResultListener& listener) noexcept
    : listener_(listener) {}

void UnavailableRendezvousClient::register_invite(const session::InviteRegistration& registration) {
    listener_.on_invite_registered(
        registration.invite_token,
        common::make_unexpected(control_protocol::ErrorCode::kUnreachable));
}

void UnavailableRendezvousClient::revoke_invite(const session::InviteCode& /*code*/,
                                                const session::RevokeKey& /*revoke_key*/) {}

void UnavailableRendezvousClient::resolve_invite(const session::InviteCode& code) {
    listener_.on_invite_resolved(
        code, common::make_unexpected(control_protocol::ErrorCode::kUnreachable));
}

}  // namespace vocroz::client
