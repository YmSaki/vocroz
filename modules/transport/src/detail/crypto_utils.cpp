// SPDX-License-Identifier: AGPL-3.0-only
//
// 暗号ライブラリの初期化の実装。

#include "detail/crypto_utils.h"

#include <noise/protocol.h>
#include <sodium.h>

namespace vocroz::transport::detail {

bool ensure_crypto_initialized() noexcept {
    // sodium_init() は初期化済みなら 1 を返すので、0 以上を成功とみなす。
    static const bool initialized = sodium_init() >= 0 && noise_init() == NOISE_ERROR_NONE;
    return initialized;
}

}  // namespace vocroz::transport::detail
