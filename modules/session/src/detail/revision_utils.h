// SPDX-License-Identifier: AGPL-3.0-only
//
// entity の revision の進め方。親機と子機のセッションで同じ規則を使う。
#pragma once

#include "vocroz/control_protocol/constants.h"
#include "vocroz/control_protocol/entities.h"

namespace vocroz::session::detail {

/// revision を 1 進める。上限（2^53 - 1、UI の数値で正確に表せる最大値）に届くには、毎秒 1 回の
/// 操作でも 2 億年以上かかるので、桁あふれは考えず上限で止めるだけにする。
/// スレッド安全性: どのスレッドからも呼び出し可能（引数だけを変える）。
inline void bump_revision(control_protocol::Revision& revision) noexcept {
    if (revision < control_protocol::kMaxCounter) {
        ++revision;
    }
}

}  // namespace vocroz::session::detail
