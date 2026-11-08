// SPDX-License-Identifier: AGPL-3.0-only
//
// libsodium による RandomSource の実装。

#include "vocroz/transport/crypto_random_source.h"

#include <sodium.h>

#include "detail/crypto_utils.h"

namespace vocroz::transport {

std::uint32_t CryptoRandomSource::next_uint32() noexcept {
    if (!detail::ensure_crypto_initialized()) {
        return 0;
    }
    return randombytes_random();
}

}  // namespace vocroz::transport
