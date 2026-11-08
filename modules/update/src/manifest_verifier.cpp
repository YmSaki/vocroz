// SPDX-License-Identifier: AGPL-3.0-only
//
// manifest_verifier.h の実装。Ed25519 の検証には libsodium を使う（暗号プリミティブを自作しない）。

#include "vocroz/update/manifest_verifier.h"

#include <sodium.h>

#include "detail/sodium_init.h"
#include "detail/text_utils.h"
#include "wire/manifest_codec.h"

namespace vocroz::update {

ManifestVerifier::ManifestVerifier(std::optional<Ed25519PublicKey> public_key)
    : public_key_(public_key) {}

ManifestResult ManifestVerifier::verify(std::string_view text) const {
    if (!public_key_) {
        return ManifestError::kNoPublicKey;
    }
    const auto signed_text = wire::split_signed_manifest(text);
    if (!signed_text) {
        return ManifestError::kMalformed;
    }
    if (!detail::ensure_sodium_initialized()) {
        // 初期化できない環境では検証の正しさを保証できないので、署名不一致として拒否する。
        return ManifestError::kBadSignature;
    }
    const auto* body = reinterpret_cast<const unsigned char*>(signed_text->body.data());
    if (crypto_sign_verify_detached(signed_text->signature.data(), body, signed_text->body.size(),
                                    public_key_->data()) != 0) {
        return ManifestError::kBadSignature;
    }
    return wire::decode_manifest_body(signed_text->body);
}

std::optional<Ed25519PublicKey> parse_public_key_hex(std::string_view hex) {
    Ed25519PublicKey key{};
    if (!detail::decode_lower_hex(hex, key)) {
        return std::nullopt;
    }
    return key;
}

}  // namespace vocroz::update
