// SPDX-License-Identifier: AGPL-3.0-only
//
// NoiseIkHandshake の単体テスト。鍵交換の往復と、改ざん・鍵の不一致・呼び出し順序の誤りを確かめる。

#include "vocroz/transport/noise_ik_handshake.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr ConnectionId kConnectionId = 0x11223344;

StaticKeyPair make_keys() {
    auto keys = StaticKeyPair::generate();
    EXPECT_TRUE(keys);
    return std::move(keys).value();
}

NoiseIkHandshake make_initiator(const StaticKeyPair& local, const PublicKey& responder) {
    auto handshake = NoiseIkHandshake::create_initiator(local, responder, kConnectionId);
    EXPECT_TRUE(handshake);
    return std::move(handshake).value();
}

NoiseIkHandshake make_responder(const StaticKeyPair& local) {
    auto handshake = NoiseIkHandshake::create_responder(local);
    EXPECT_TRUE(handshake);
    return std::move(handshake).value();
}

Bytes write_initiation(NoiseIkHandshake& initiator, std::span<const std::uint8_t> payload) {
    Bytes packet(kMaxPacketBytes);
    const auto written = initiator.write_initiation(payload, packet);
    EXPECT_TRUE(written);
    packet.resize(written ? written.value() : 0);
    return packet;
}

Bytes write_response(NoiseIkHandshake& responder, std::span<const std::uint8_t> payload) {
    Bytes packet(kMaxPacketBytes);
    const auto written = responder.write_response(payload, packet);
    EXPECT_TRUE(written);
    packet.resize(written ? written.value() : 0);
    return packet;
}

class NoiseIkHandshakeTest : public ::testing::Test {
protected:
    StaticKeyPair client_keys_ = make_keys();
    StaticKeyPair host_keys_ = make_keys();
    NoiseIkHandshake initiator_ = make_initiator(client_keys_, host_keys_.public_key());
    NoiseIkHandshake responder_ = make_responder(host_keys_);
    Bytes payload_buffer_ = Bytes(kMaxPacketBytes);
};

TEST_F(NoiseIkHandshakeTest, RoundTripExchangesPayloadsAndEstablishesChannel) {
    const Bytes invite_token{'t', 'o', 'k', 'e', 'n'};
    const Bytes initiation = write_initiation(initiator_, invite_token);
    EXPECT_EQ(initiation.size(), kHandshakeInitiationOverheadBytes + invite_token.size());

    const auto read_token = responder_.read_initiation(initiation, payload_buffer_);
    ASSERT_TRUE(read_token);
    EXPECT_EQ(Bytes(payload_buffer_.begin(),
                    payload_buffer_.begin() + static_cast<std::ptrdiff_t>(read_token.value())),
              invite_token);
    EXPECT_EQ(responder_.connection_id(), kConnectionId);
    // 応答側は 1 つ目のメッセージで開始側の静的公開鍵を知り、session の承認に使える。
    const auto client_public = responder_.remote_static_public_key();
    ASSERT_TRUE(client_public);
    EXPECT_EQ(client_public.value(), client_keys_.public_key());

    const Bytes accepted{'o', 'k'};
    const Bytes response = write_response(responder_, accepted);
    EXPECT_EQ(response.size(), kHandshakeResponseOverheadBytes + accepted.size());
    const auto read_accepted = initiator_.read_response(response, payload_buffer_);
    ASSERT_TRUE(read_accepted);
    EXPECT_EQ(Bytes(payload_buffer_.begin(),
                    payload_buffer_.begin() + static_cast<std::ptrdiff_t>(read_accepted.value())),
              accepted);

    auto client_channel = initiator_.finish();
    auto host_channel = responder_.finish();
    ASSERT_TRUE(client_channel);
    ASSERT_TRUE(host_channel);
    EXPECT_EQ(client_channel.value().sealer.connection_id(), kConnectionId);
    EXPECT_EQ(host_channel.value().opener.connection_id(), kConnectionId);

    // 両方向で、相手の送信鍵と自分の受信鍵が一致する。
    const Bytes message{1, 2, 3};
    Bytes packet(kMaxPacketBytes);
    Bytes plaintext(kMaxPacketBytes);
    const auto upstream = client_channel.value().sealer.seal_control(message, packet);
    ASSERT_TRUE(upstream);
    const auto opened_upstream =
        host_channel.value().opener.open(std::span(packet).first(upstream.value()), plaintext);
    ASSERT_TRUE(opened_upstream);
    EXPECT_EQ(
        Bytes(opened_upstream.value().plaintext.begin(), opened_upstream.value().plaintext.end()),
        message);

    const auto downstream = host_channel.value().sealer.seal_control(message, packet);
    ASSERT_TRUE(downstream);
    const auto opened_downstream =
        client_channel.value().opener.open(std::span(packet).first(downstream.value()), plaintext);
    ASSERT_TRUE(opened_downstream);
}

TEST_F(NoiseIkHandshakeTest, EmptyPayloadsAreAllowed) {
    const Bytes initiation = write_initiation(initiator_, {});
    const auto read = responder_.read_initiation(initiation, payload_buffer_);
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value(), 0U);
    const Bytes response = write_response(responder_, {});
    ASSERT_TRUE(initiator_.read_response(response, payload_buffer_));
    EXPECT_TRUE(initiator_.finish());
    EXPECT_TRUE(responder_.finish());
}

TEST_F(NoiseIkHandshakeTest, InitiationForAnotherHostIsRejected) {
    // 子機が想定した親機の公開鍵と、実際に応答する親機の鍵が違う（なりすまし先の取り違え）。
    const StaticKeyPair other_host = make_keys();
    NoiseIkHandshake initiator = make_initiator(client_keys_, other_host.public_key());
    const Bytes initiation = write_initiation(initiator, {});
    const auto read = responder_.read_initiation(initiation, payload_buffer_);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error(), TransportError::kHandshakeFailed);
}

TEST_F(NoiseIkHandshakeTest, ResponseFromImpersonatorIsRejected) {
    // 中間者が自分の鍵で応答しても、子機は親機の公開鍵を知っているので完了しない。
    const Bytes initiation = write_initiation(initiator_, {});
    const StaticKeyPair impostor_keys = make_keys();
    NoiseIkHandshake impostor = make_responder(impostor_keys);
    const auto read_by_impostor = impostor.read_initiation(initiation, payload_buffer_);
    ASSERT_FALSE(read_by_impostor);
    EXPECT_EQ(read_by_impostor.error(), TransportError::kHandshakeFailed);
}

TEST_F(NoiseIkHandshakeTest, TamperedInitiationIsRejectedAtEveryByte) {
    const Bytes initiation = write_initiation(initiator_, Bytes{1, 2, 3});
    // ヘッダーのバージョンと種別（0、1 バイト目）は別のエラーになるので、接続 ID 以降を確かめる。
    // 接続 ID はプロローグに混ぜているので、平文のヘッダーでも書き換えを検出できる。
    for (std::size_t i = 2; i < initiation.size(); ++i) {
        if (i >= 6 && i < kPacketHeaderBytes) {
            continue;  // シーケンス番号はハンドシェイクでは使わない
        }
        Bytes tampered = initiation;
        tampered[i] ^= 0x01;
        NoiseIkHandshake responder = make_responder(host_keys_);
        const auto read = responder.read_initiation(tampered, payload_buffer_);
        ASSERT_FALSE(read) << "byte " << i;
        EXPECT_EQ(read.error(), TransportError::kHandshakeFailed) << "byte " << i;
    }
}

TEST_F(NoiseIkHandshakeTest, TamperedResponseFailsHandshake) {
    const Bytes initiation = write_initiation(initiator_, {});
    ASSERT_TRUE(responder_.read_initiation(initiation, payload_buffer_));
    Bytes response = write_response(responder_, Bytes{9});
    response.back() ^= 0x80;
    const auto read = initiator_.read_response(response, payload_buffer_);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error(), TransportError::kHandshakeFailed);
    // 一度失敗したハンドシェイクは鍵を出さない。
    const auto channel = initiator_.finish();
    ASSERT_FALSE(channel);
    EXPECT_EQ(channel.error(), TransportError::kInvalidState);
}

TEST_F(NoiseIkHandshakeTest, ResponseForOtherConnectionDoesNotDisturbHandshake) {
    const Bytes initiation = write_initiation(initiator_, {});
    ASSERT_TRUE(responder_.read_initiation(initiation, payload_buffer_));
    const Bytes response = write_response(responder_, {});
    Bytes stray = response;
    stray[2] ^= 0xFF;  // 別の接続宛てのパケット
    const auto read_stray = initiator_.read_response(stray, payload_buffer_);
    ASSERT_FALSE(read_stray);
    EXPECT_EQ(read_stray.error(), TransportError::kConnectionMismatch);
    // 無関係なパケットでは失敗させず、本物の応答で完了できる。
    EXPECT_TRUE(initiator_.read_response(response, payload_buffer_));
    EXPECT_TRUE(initiator_.finish());
}

TEST_F(NoiseIkHandshakeTest, TruncatedInitiationIsRejectedWithoutFailingResponder) {
    const Bytes initiation = write_initiation(initiator_, {});
    const Bytes truncated(initiation.begin(), initiation.end() - 1);
    const auto read_truncated = responder_.read_initiation(truncated, payload_buffer_);
    ASSERT_FALSE(read_truncated);
    EXPECT_EQ(read_truncated.error(), TransportError::kTruncated);
    EXPECT_TRUE(responder_.read_initiation(initiation, payload_buffer_));
}

TEST_F(NoiseIkHandshakeTest, RejectsWrongPacketType) {
    const Bytes initiation = write_initiation(initiator_, {});
    const auto read = initiator_.read_response(initiation, payload_buffer_);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error(), TransportError::kUnexpectedPacketType);
}

TEST_F(NoiseIkHandshakeTest, RejectsCallsOutOfOrder) {
    const auto early_finish = initiator_.finish();
    ASSERT_FALSE(early_finish);
    EXPECT_EQ(early_finish.error(), TransportError::kInvalidState);

    Bytes out(kMaxPacketBytes);
    const auto early_response = responder_.write_response({}, out);
    ASSERT_FALSE(early_response);
    EXPECT_EQ(early_response.error(), TransportError::kInvalidState);
    // 応答側は開始側のメッセージを書けない。
    const auto wrong_role = responder_.write_initiation({}, out);
    ASSERT_FALSE(wrong_role);
    EXPECT_EQ(wrong_role.error(), TransportError::kInvalidState);

    const auto remote_before_read = responder_.remote_static_public_key();
    ASSERT_FALSE(remote_before_read);
    EXPECT_EQ(remote_before_read.error(), TransportError::kInvalidState);

    ASSERT_TRUE(initiator_.write_initiation({}, out));
    const auto twice = initiator_.write_initiation({}, out);
    ASSERT_FALSE(twice);
    EXPECT_EQ(twice.error(), TransportError::kInvalidState);
}

TEST_F(NoiseIkHandshakeTest, FinishCanBeCalledOnlyOnce) {
    const Bytes initiation = write_initiation(initiator_, {});
    ASSERT_TRUE(responder_.read_initiation(initiation, payload_buffer_));
    ASSERT_TRUE(initiator_.read_response(write_response(responder_, {}), payload_buffer_));
    ASSERT_TRUE(initiator_.finish());
    const auto again = initiator_.finish();
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error(), TransportError::kInvalidState);
}

TEST_F(NoiseIkHandshakeTest, RejectsOversizedPayloadAndSmallBuffer) {
    const Bytes too_large(kMaxHandshakeInitiationPayloadBytes + 1);
    Bytes out(kMaxPacketBytes);
    const auto oversized = initiator_.write_initiation(too_large, out);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error(), TransportError::kPayloadTooLarge);

    Bytes small(kHandshakeInitiationOverheadBytes - 1);
    const auto too_small = initiator_.write_initiation({}, small);
    ASSERT_FALSE(too_small);
    EXPECT_EQ(too_small.error(), TransportError::kBufferTooSmall);

    // 長さの誤りでは状態を変えず、正しい呼び出しで続けられる。
    const Bytes largest(kMaxHandshakeInitiationPayloadBytes, 0x5A);
    const auto written = initiator_.write_initiation(largest, out);
    ASSERT_TRUE(written);
    EXPECT_EQ(written.value(), kMaxPacketBytes);
}

TEST(GenerateConnectionIdTest, ProducesRandomIds) {
    const auto first = generate_connection_id();
    const auto second = generate_connection_id();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    // 32 ビットの乱数が 2 回続けて一致する確率は無視できる。
    EXPECT_NE(first.value(), second.value());
}

}  // namespace
}  // namespace vocroz::transport
