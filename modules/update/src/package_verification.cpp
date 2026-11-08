// SPDX-License-Identifier: AGPL-3.0-only
//
// package_verification.h の実装。SHA-256 は libsodium を使う。

#include "package_verification.h"

#include <cstdint>
#include <span>

#include <sodium.h>

#include "detail/sodium_init.h"

namespace vocroz::update {

bool package_matches_manifest(ports::StagedPackage& package, const UpdateManifest& manifest) {
    if (!detail::ensure_sodium_initialized()) {
        return false;
    }
    crypto_hash_sha256_state hash_state;
    crypto_hash_sha256_init(&hash_state);
    std::uint64_t total = 0;
    bool oversized = false;
    const bool read_all = package.read([&](std::span<const std::uint8_t> chunk) {
        total += chunk.size();
        // マニフェストより大きいパッケージは、それ以上ハッシュに通さずに不一致とする。
        if (total > manifest.package_size) {
            oversized = true;
            return;
        }
        crypto_hash_sha256_update(&hash_state, chunk.data(), chunk.size());
    });
    Sha256Digest digest{};
    crypto_hash_sha256_final(&hash_state, digest.data());
    if (!read_all || oversized || total != manifest.package_size) {
        return false;
    }
    return sodium_memcmp(digest.data(), manifest.package_sha256.data(), digest.size()) == 0;
}

}  // namespace vocroz::update
