// SPDX-License-Identifier: AGPL-3.0-only
//
// マニフェストを扱うテスト（ManifestVerifier、UpdateApplier）が共有する、テスト用の鍵と署名。
// 鍵は固定のシードから作り、テストを決定的にする。
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <sodium.h>

#include "vocroz/update/manifest_verifier.h"

namespace vocroz::update::testing {

/// テスト用の Ed25519 の鍵ペア。
struct TestKeyPair {
    Ed25519PublicKey public_key{};
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secret_key{};
};

/// シードのすべてのバイトを fill にした鍵ペアを作る。
inline TestKeyPair make_key_pair(std::uint8_t fill) {
    if (sodium_init() < 0) {
        return {};
    }
    std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
    seed.fill(fill);
    TestKeyPair pair;
    crypto_sign_seed_keypair(pair.public_key.data(), pair.secret_key.data(), seed.data());
    return pair;
}

/// 本文に署名の行を加えたマニフェストを返す。
inline std::string sign_manifest(std::string_view body, const TestKeyPair& key) {
    std::array<std::uint8_t, crypto_sign_BYTES> signature{};
    crypto_sign_detached(signature.data(), nullptr,
                         reinterpret_cast<const unsigned char*>(body.data()), body.size(),
                         key.secret_key.data());
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text{body};
    text += "signature=";
    for (const std::uint8_t byte : signature) {
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0F];
    }
    text += '\n';
    return text;
}

/// バイト列の SHA-256 を小文字の 16 進数で返す。
inline std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    std::array<std::uint8_t, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), bytes.data(), bytes.size());
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : digest) {
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0F];
    }
    return text;
}

/// 正しい形式の本文を組み立てる。
inline std::string manifest_body(std::string_view version, std::string_view sha256,
                                 std::uint64_t size, std::string_view publisher = "",
                                 std::string_view url = "https://example.invalid/pkg.zip") {
    std::string body = "format=1\nproduct=vocroz\n";
    body += "version=" + std::string{version} + '\n';
    body += "url=" + std::string{url} + '\n';
    body += "sha256=" + std::string{sha256} + '\n';
    body += "size=" + std::to_string(size) + '\n';
    body += "publisher=" + std::string{publisher} + '\n';
    return body;
}

}  // namespace vocroz::update::testing
