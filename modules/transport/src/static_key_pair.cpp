// SPDX-License-Identifier: AGPL-3.0-only
//
// Curve25519 の静的鍵ペアの生成と、秘密鍵の消去。

#include "vocroz/transport/static_key_pair.h"

#include <algorithm>

#include <sodium.h>

#include "detail/crypto_utils.h"

namespace vocroz::transport {

static_assert(crypto_box_PUBLICKEYBYTES == kPublicKeyBytes);
static_assert(crypto_box_SECRETKEYBYTES == kPrivateKeyBytes);

common::Result<StaticKeyPair, TransportError> StaticKeyPair::generate() noexcept {
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    StaticKeyPair keys;
    // crypto_box の鍵ペアは X25519（Curve25519）の鍵で、Noise の 25519 と同じ形式。
    crypto_box_keypair(keys.public_key_.data(), keys.private_key_.data());
    return keys;
}

common::Result<StaticKeyPair, TransportError> StaticKeyPair::from_private_key(
    std::span<const std::uint8_t> private_key) noexcept {
    if (private_key.size() != kPrivateKeyBytes) {
        return common::make_unexpected(TransportError::kInvalidKey);
    }
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(TransportError::kCryptoUnavailable);
    }
    StaticKeyPair keys;
    std::copy(private_key.begin(), private_key.end(), keys.private_key_.begin());
    // crypto_box の公開鍵は秘密鍵の X25519 の基点倍。generate() と同じ鍵ペアになる。
    if (crypto_scalarmult_base(keys.public_key_.data(), keys.private_key_.data()) != 0) {
        return common::make_unexpected(TransportError::kInvalidKey);
    }
    return keys;
}

StaticKeyPair::StaticKeyPair(StaticKeyPair&& other) noexcept
    : public_key_(other.public_key_), private_key_(other.private_key_) {
    sodium_memzero(other.private_key_.data(), other.private_key_.size());
}

StaticKeyPair& StaticKeyPair::operator=(StaticKeyPair&& other) noexcept {
    if (this != &other) {
        public_key_ = other.public_key_;
        private_key_ = other.private_key_;
        sodium_memzero(other.private_key_.data(), other.private_key_.size());
    }
    return *this;
}

StaticKeyPair::~StaticKeyPair() {
    sodium_memzero(private_key_.data(), private_key_.size());
}

}  // namespace vocroz::transport
