// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待の登録の結果を session に渡す処理の型。ランデブーサーバーとの通信の実装
// （HTTPS の InviteResultRelay と、オフラインの OfflineRendezvousClient）が共通に使う。
#pragma once

#include <functional>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/ports/rendezvous_client.h"

namespace vocroz::engine {

/// 登録の結果を受け取る処理（HostSession::on_invite_registered()）。コントロールスレッドで呼ぶ。
using InviteRegistrationHandler = std::function<void(
    const session::InviteToken&,
    const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>&)>;

}  // namespace vocroz::engine
