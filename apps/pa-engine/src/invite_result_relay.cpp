// SPDX-License-Identifier: AGPL-3.0-only
//
// invite_result_relay.h の実装。

#include "invite_result_relay.h"

namespace vocroz::engine {

void InviteResultRelay::on_invite_registered(
    const session::InviteToken& token,
    const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>& result) {
    // 結果はコピーして積む。作業用のスレッドの値は、この呼び出しから戻ると使えない。
    loop_.post([this, token, result] {
        if (handler_) {
            handler_(token, result);
        }
    });
}

void InviteResultRelay::on_invite_resolved(
    const session::InviteCode& /*code*/,
    const common::Result<session::ResolvedInvite, control_protocol::ErrorCode>& /*result*/) {
    // 親機は招待を解決しない（子機だけが使う）。
}

}  // namespace vocroz::engine
