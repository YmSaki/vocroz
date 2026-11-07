// SPDX-License-Identifier: AGPL-3.0-only
//
// noise-c のリンク確認テスト。VOCROZ が使う Noise_IK_25519_ChaChaPoly_BLAKE2s で
// ハンドシェイクを完了し、確立した鍵で 1 メッセージを暗号化して復号できることを確かめる。

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>
#include <noise/protocol.h>

namespace {

// docs/adr/0001-key-exchange-noise-ik.md で決めたプロトコル名。
constexpr const char* kProtocolName = "Noise_IK_25519_ChaChaPoly_BLAKE2s";
// ハンドシェイクのメッセージの上限。Noise のメッセージの最大長（65535 バイト）に合わせる。
constexpr std::size_t kMaxMessageBytes = 65535;
// Curve25519 の公開鍵の長さ。
constexpr std::size_t kPublicKeyBytes = 32;
// ChaChaPoly の認証タグの長さ。
constexpr std::size_t kMacBytes = 16;

// 書き込む側の 1 メッセージを、読み込む側に渡す。
void transfer_message(NoiseHandshakeState* writer, NoiseHandshakeState* reader) {
    std::array<std::uint8_t, kMaxMessageBytes> data{};
    NoiseBuffer buffer;
    noise_buffer_set_output(buffer, data.data(), data.size());
    ASSERT_EQ(noise_handshakestate_write_message(writer, &buffer, nullptr), NOISE_ERROR_NONE);
    noise_buffer_set_input(buffer, data.data(), buffer.size);
    ASSERT_EQ(noise_handshakestate_read_message(reader, &buffer, nullptr), NOISE_ERROR_NONE);
}

TEST(NoiseCLinkTest, CompletesIkHandshakeAndRoundTripsMessage) {
    ASSERT_EQ(noise_init(), NOISE_ERROR_NONE);

    NoiseHandshakeState* initiator = nullptr;
    NoiseHandshakeState* responder = nullptr;
    ASSERT_EQ(noise_handshakestate_new_by_name(&initiator, kProtocolName, NOISE_ROLE_INITIATOR),
              NOISE_ERROR_NONE);
    ASSERT_EQ(noise_handshakestate_new_by_name(&responder, kProtocolName, NOISE_ROLE_RESPONDER),
              NOISE_ERROR_NONE);

    // IK では、開始側（子機）が応答側（親機）の静的公開鍵を事前に知っている。
    NoiseDHState* responder_static = noise_handshakestate_get_local_keypair_dh(responder);
    ASSERT_EQ(noise_dhstate_generate_keypair(responder_static), NOISE_ERROR_NONE);
    std::array<std::uint8_t, kPublicKeyBytes> responder_public{};
    ASSERT_EQ(noise_dhstate_get_public_key(responder_static, responder_public.data(),
                                           responder_public.size()),
              NOISE_ERROR_NONE);
    ASSERT_EQ(noise_dhstate_generate_keypair(noise_handshakestate_get_local_keypair_dh(initiator)),
              NOISE_ERROR_NONE);
    ASSERT_EQ(noise_dhstate_set_public_key(noise_handshakestate_get_remote_public_key_dh(initiator),
                                           responder_public.data(), responder_public.size()),
              NOISE_ERROR_NONE);

    ASSERT_EQ(noise_handshakestate_start(initiator), NOISE_ERROR_NONE);
    ASSERT_EQ(noise_handshakestate_start(responder), NOISE_ERROR_NONE);

    // IK は 2 メッセージ（開始側 → 応答側、応答側 → 開始側）で完了する。
    transfer_message(initiator, responder);
    transfer_message(responder, initiator);
    ASSERT_EQ(noise_handshakestate_get_action(initiator), NOISE_ACTION_SPLIT);
    ASSERT_EQ(noise_handshakestate_get_action(responder), NOISE_ACTION_SPLIT);

    NoiseCipherState* initiator_send = nullptr;
    NoiseCipherState* initiator_receive = nullptr;
    NoiseCipherState* responder_send = nullptr;
    NoiseCipherState* responder_receive = nullptr;
    ASSERT_EQ(noise_handshakestate_split(initiator, &initiator_send, &initiator_receive),
              NOISE_ERROR_NONE);
    ASSERT_EQ(noise_handshakestate_split(responder, &responder_send, &responder_receive),
              NOISE_ERROR_NONE);

    const std::array<std::uint8_t, 4> message{1, 2, 3, 4};
    std::array<std::uint8_t, message.size() + kMacBytes> data{};
    std::copy(message.begin(), message.end(), data.begin());
    NoiseBuffer buffer;
    noise_buffer_set_inout(buffer, data.data(), message.size(), data.size());
    ASSERT_EQ(noise_cipherstate_encrypt(initiator_send, &buffer), NOISE_ERROR_NONE);
    ASSERT_EQ(noise_cipherstate_decrypt(responder_receive, &buffer), NOISE_ERROR_NONE);
    ASSERT_EQ(buffer.size, message.size());
    EXPECT_TRUE(std::equal(message.begin(), message.end(), data.begin()));

    noise_cipherstate_free(initiator_send);
    noise_cipherstate_free(initiator_receive);
    noise_cipherstate_free(responder_send);
    noise_cipherstate_free(responder_receive);
    noise_handshakestate_free(initiator);
    noise_handshakestate_free(responder);
}

}  // namespace
