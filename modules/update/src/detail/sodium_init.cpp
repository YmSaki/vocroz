// SPDX-License-Identifier: AGPL-3.0-only
//
// sodium_init.h の実装。

#include "detail/sodium_init.h"

#include <sodium.h>

namespace vocroz::update::detail {

bool ensure_sodium_initialized() noexcept {
    // sodium_init() は初期化済みなら 1、成功なら 0、失敗なら -1 を返す。
    return sodium_init() >= 0;
}

}  // namespace vocroz::update::detail
