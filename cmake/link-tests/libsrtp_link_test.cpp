// SPDX-License-Identifier: AGPL-3.0-only
//
// libsrtp のリンク確認テスト。内蔵の暗号の実装で、AES_CM_128_HMAC_SHA1_80 の送信側と受信側の
// セッションを作り、RTP のパケットを 1 つ暗号化して復号できることを確かめる。

#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>
#include <srtp.h>

namespace {

constexpr std::uint32_t kSsrc = 0x12345678;
constexpr int kRtpHeaderBytes = 12;
constexpr int kPayloadBytes = 20;

srtp_policy_t make_policy(std::array<unsigned char, SRTP_AES_ICM_128_KEY_LEN_WSALT>& key,
                          srtp_ssrc_type_t direction) {
    srtp_policy_t policy;
    std::memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
    policy.ssrc.type = direction;
    policy.ssrc.value = kSsrc;
    policy.key = key.data();
    policy.window_size = 128;
    return policy;
}

TEST(LibsrtpLinkTest, ProtectsAndUnprotectsOnePacket) {
    ASSERT_EQ(srtp_init(), srtp_err_status_ok);
    std::array<unsigned char, SRTP_AES_ICM_128_KEY_LEN_WSALT> key{};
    key.fill(0x5A);

    srtp_policy_t send_policy = make_policy(key, ssrc_specific);
    srtp_t sender = nullptr;
    ASSERT_EQ(srtp_create(&sender, &send_policy), srtp_err_status_ok);
    srtp_policy_t receive_policy = make_policy(key, ssrc_any_inbound);
    srtp_t receiver = nullptr;
    ASSERT_EQ(srtp_create(&receiver, &receive_policy), srtp_err_status_ok);

    // V=2、PT=111、シーケンス番号 1、タイムスタンプ 0、SSRC kSsrc の RTP ヘッダーとペイロード。
    alignas(4) std::array<std::uint8_t, kRtpHeaderBytes + kPayloadBytes + SRTP_MAX_TRAILER_LEN>
        packet{};
    packet[0] = 0x80;
    packet[1] = 111;
    packet[3] = 1;
    packet[8] = 0x12;
    packet[9] = 0x34;
    packet[10] = 0x56;
    packet[11] = 0x78;
    for (int i = 0; i < kPayloadBytes; ++i) {
        packet[kRtpHeaderBytes + i] = static_cast<std::uint8_t>(i);
    }
    const auto original = packet;

    int length = kRtpHeaderBytes + kPayloadBytes;
    ASSERT_EQ(srtp_protect(sender, packet.data(), &length), srtp_err_status_ok);
    EXPECT_EQ(length, kRtpHeaderBytes + kPayloadBytes + 10);  // 80 ビットの認証タグ

    ASSERT_EQ(srtp_unprotect(receiver, packet.data(), &length), srtp_err_status_ok);
    ASSERT_EQ(length, kRtpHeaderBytes + kPayloadBytes);
    EXPECT_EQ(std::memcmp(packet.data(), original.data(), static_cast<std::size_t>(length)), 0);

    srtp_dealloc(receiver);
    srtp_dealloc(sender);
}

}  // namespace
