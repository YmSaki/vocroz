// SPDX-License-Identifier: AGPL-3.0-only
//
// libsodium のリンク確認テスト。初期化と、ChaCha20-Poly1305 の暗号化と復号の往復を確かめる。

#include <array>

#include <gtest/gtest.h>
#include <sodium.h>

namespace {

TEST(LibsodiumLinkTest, RoundTripsChaCha20Poly1305) {
    ASSERT_GE(sodium_init(), 0);

    std::array<unsigned char, crypto_aead_chacha20poly1305_ietf_KEYBYTES> key{};
    std::array<unsigned char, crypto_aead_chacha20poly1305_ietf_NPUBBYTES> nonce{};
    crypto_aead_chacha20poly1305_ietf_keygen(key.data());
    randombytes_buf(nonce.data(), nonce.size());

    const std::array<unsigned char, 4> message{1, 2, 3, 4};
    std::array<unsigned char, message.size() + crypto_aead_chacha20poly1305_ietf_ABYTES> cipher{};
    unsigned long long cipher_len = 0;
    ASSERT_EQ(crypto_aead_chacha20poly1305_ietf_encrypt(cipher.data(), &cipher_len, message.data(),
                                                        message.size(), nullptr, 0, nullptr,
                                                        nonce.data(), key.data()),
              0);

    std::array<unsigned char, message.size()> decrypted{};
    unsigned long long decrypted_len = 0;
    ASSERT_EQ(crypto_aead_chacha20poly1305_ietf_decrypt(decrypted.data(), &decrypted_len, nullptr,
                                                        cipher.data(), cipher_len, nullptr, 0,
                                                        nonce.data(), key.data()),
              0);
    EXPECT_EQ(decrypted, message);
}

}  // namespace
