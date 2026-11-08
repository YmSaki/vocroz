// SPDX-License-Identifier: AGPL-3.0-only
//
// PacketSealer と PacketOpener の単体テスト。
// 改ざん、リプレイ、切り詰めたパケットを破棄し、破棄しても以降の正規のパケットに影響しないことを確かめる
// （AT-A17 のドメイン部分）。

#include "vocroz/transport/secure_channel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/noise_ik_handshake.h"

namespace vocroz::transport {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr ConnectionId kConnectionId = 0xCAFEF00D;

// Noise_IK のハンドシェイクを最後まで行い、子機側と親機側の SecureChannel を返す。
std::pair<SecureChannel, SecureChannel> establish() {
    auto client_keys = StaticKeyPair::generate();
    auto host_keys = StaticKeyPair::generate();
    EXPECT_TRUE(client_keys && host_keys);
    auto initiator = NoiseIkHandshake::create_initiator(
        client_keys.value(), host_keys.value().public_key(), kConnectionId);
    auto responder = NoiseIkHandshake::create_responder(host_keys.value());
    EXPECT_TRUE(initiator && responder);
    Bytes packet(kMaxPacketBytes);
    Bytes payload(kMaxPacketBytes);
    const auto initiation = initiator.value().write_initiation({}, packet);
    EXPECT_TRUE(
        responder.value().read_initiation(std::span(packet).first(initiation.value()), payload));
    const auto response = responder.value().write_response({}, packet);
    EXPECT_TRUE(
        initiator.value().read_response(std::span(packet).first(response.value()), payload));
    auto client = initiator.value().finish();
    auto host = responder.value().finish();
    EXPECT_TRUE(client && host);
    return {std::move(client).value(), std::move(host).value()};
}

class SecureChannelTest : public ::testing::Test {
protected:
    // 子機から制御パケットを 1 つ作る。
    Bytes seal_control(const Bytes& message) {
        Bytes packet(kMaxPacketBytes);
        const auto written = client_.sealer.seal_control(message, packet);
        EXPECT_TRUE(written);
        packet.resize(written ? written.value() : 0);
        return packet;
    }

    // 親機で受信する。
    common::Result<OpenedPacket, TransportError> open(const Bytes& packet) {
        return host_.opener.open(packet, plaintext_);
    }

    std::pair<SecureChannel, SecureChannel> channels_ = establish();
    SecureChannel& client_ = channels_.first;
    SecureChannel& host_ = channels_.second;
    Bytes plaintext_ = Bytes(kMaxPacketBytes);
};

TEST_F(SecureChannelTest, MediaRoundTripPreservesFields) {
    const std::array<std::uint8_t, 3> opus{0xF8, 0xFF, 0xFE};
    const MediaPayload sent{
        .stream_id = 7, .timestamp = 48000ULL * 3600 * 30, .mute_epoch = 42, .opus_payload = opus};
    Bytes packet(kMaxPacketBytes);
    const auto written = client_.sealer.seal_media(sent, packet);
    ASSERT_TRUE(written);
    EXPECT_EQ(written.value(), sealed_packet_bytes(kMediaPayloadHeaderBytes + opus.size()));
    packet.resize(written.value());

    const auto opened = open(packet);
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened.value().type, PacketType::kMedia);
    EXPECT_EQ(opened.value().sequence, 0U);
    const auto received = decode_media_payload(opened.value().plaintext);
    ASSERT_TRUE(received);
    EXPECT_EQ(received.value().stream_id, sent.stream_id);
    EXPECT_EQ(received.value().timestamp, sent.timestamp);
    EXPECT_EQ(received.value().mute_epoch, sent.mute_epoch);
    EXPECT_TRUE(std::equal(opus.begin(), opus.end(), received.value().opus_payload.begin(),
                           received.value().opus_payload.end()));
}

TEST_F(SecureChannelTest, ControlRoundTripIncludingEmptyKeepalive) {
    const auto first = open(seal_control(Bytes{'h', 'i'}));
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value().type, PacketType::kControl);
    EXPECT_EQ(Bytes(first.value().plaintext.begin(), first.value().plaintext.end()),
              (Bytes{'h', 'i'}));

    const Bytes keepalive = seal_control({});
    EXPECT_EQ(keepalive.size(), sealed_packet_bytes(0));
    const auto second = open(keepalive);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value().sequence, 1U);
    EXPECT_TRUE(second.value().plaintext.empty());
}

TEST_F(SecureChannelTest, PayloadIsNotVisibleOnTheWire) {
    // 経路上で平文が見えないこと（AT-A17 のパケットキャプチャ確認に相当する）。
    const Bytes secret(64, 0x41);
    const Bytes packet = seal_control(secret);
    const auto body = std::span(packet).subspan(kPacketHeaderBytes);
    const auto found = std::search(body.begin(), body.end(), secret.begin(), secret.begin() + 8);
    EXPECT_EQ(found, body.end());
}

TEST_F(SecureChannelTest, ReplayedPacketIsRejected) {
    const Bytes packet = seal_control(Bytes{1});
    ASSERT_TRUE(open(packet));
    const auto replayed = open(packet);
    ASSERT_FALSE(replayed);
    EXPECT_EQ(replayed.error(), TransportError::kReplayed);
}

TEST_F(SecureChannelTest, AcceptsReorderedPacketsWithinWindowOnce) {
    std::vector<Bytes> packets;
    for (int i = 0; i < 5; ++i) {
        packets.push_back(seal_control(Bytes{static_cast<std::uint8_t>(i)}));
    }
    for (const int index : {4, 1, 3, 0, 2}) {
        const auto opened = open(packets[static_cast<std::size_t>(index)]);
        ASSERT_TRUE(opened) << "index " << index;
        EXPECT_EQ(opened.value().sequence, static_cast<std::uint64_t>(index));
    }
    for (const auto& packet : packets) {
        const auto again = open(packet);
        ASSERT_FALSE(again);
        EXPECT_EQ(again.error(), TransportError::kReplayed);
    }
}

TEST_F(SecureChannelTest, RejectsPacketsOlderThanWindow) {
    const Bytes oldest = seal_control({});
    const Bytes edge = seal_control({});
    std::vector<Bytes> later;
    for (std::size_t i = 0; i + 1 < PacketOpener::kReplayWindowPackets; ++i) {
        later.push_back(seal_control({}));
    }
    // 最新はシーケンス番号 kReplayWindowPackets。0 は窓の外（最新との差が窓の幅）、1 は窓の端。
    ASSERT_TRUE(open(later.back()));
    const auto too_old = open(oldest);
    ASSERT_FALSE(too_old);
    EXPECT_EQ(too_old.error(), TransportError::kReplayed);
    EXPECT_TRUE(open(edge));
    EXPECT_TRUE(open(later[later.size() / 2]));
}

TEST_F(SecureChannelTest, WindowShiftKeepsHistoryAcrossWordBoundaries) {
    // 窓のずらし方（語をまたぐシフト）を確かめるため、間隔を空けて受信してから古い番号を再送する。
    std::vector<Bytes> packets;
    for (int i = 0; i < 300; ++i) {
        packets.push_back(seal_control({}));
    }
    for (const std::size_t index : {0U, 63U, 64U, 130U, 299U}) {
        ASSERT_TRUE(open(packets[index])) << "index " << index;
    }
    for (const std::size_t index : {0U, 63U, 64U, 130U}) {
        const auto again = open(packets[index]);
        ASSERT_FALSE(again) << "index " << index;
        EXPECT_EQ(again.error(), TransportError::kReplayed);
    }
    for (const std::size_t index : {1U, 62U, 65U, 129U, 131U, 298U}) {
        EXPECT_TRUE(open(packets[index])) << "index " << index;
    }
}

TEST_F(SecureChannelTest, TamperingAnyByteIsDetectedAndDoesNotAffectLaterPackets) {
    const Bytes packet = seal_control(Bytes{1, 2, 3, 4});
    for (std::size_t i = 0; i < packet.size(); ++i) {
        Bytes tampered = packet;
        tampered[i] ^= 0x01;
        EXPECT_FALSE(open(tampered)) << "byte " << i;
    }
    // 改ざんしたパケットは窓に記録されないので、本物はまだ受け付ける。
    EXPECT_TRUE(open(packet));
}

TEST_F(SecureChannelTest, TamperedCiphertextAndHeaderFailAuthentication) {
    const Bytes packet = seal_control(Bytes{1, 2, 3, 4});
    Bytes ciphertext_flipped = packet;
    ciphertext_flipped[kPacketHeaderBytes] ^= 0x80;
    EXPECT_EQ(open(ciphertext_flipped).error(), TransportError::kAuthenticationFailed);

    Bytes tag_flipped = packet;
    tag_flipped.back() ^= 0x80;
    EXPECT_EQ(open(tag_flipped).error(), TransportError::kAuthenticationFailed);

    // ヘッダーは関連データなので、種別やシーケンス番号の書き換えも認証で検出する。
    Bytes type_changed = packet;
    type_changed[1] = static_cast<std::uint8_t>(PacketType::kMedia);
    EXPECT_EQ(open(type_changed).error(), TransportError::kAuthenticationFailed);

    Bytes sequence_changed = packet;
    sequence_changed[kPacketHeaderBytes - 1] ^= 0x01;
    EXPECT_EQ(open(sequence_changed).error(), TransportError::kAuthenticationFailed);

    Bytes connection_changed = packet;
    connection_changed[2] ^= 0x01;
    EXPECT_EQ(open(connection_changed).error(), TransportError::kConnectionMismatch);
}

TEST_F(SecureChannelTest, ForgedHighSequenceDoesNotAdvanceWindow) {
    const Bytes genuine = seal_control({});
    // 攻撃者が大きなシーケンス番号のパケットを偽造しても、認証に失敗するので窓は進まない。
    Bytes forged = genuine;
    std::fill(forged.begin() + 6, forged.begin() + static_cast<std::ptrdiff_t>(kPacketHeaderBytes),
              std::uint8_t{0x7F});
    EXPECT_EQ(open(forged).error(), TransportError::kAuthenticationFailed);
    EXPECT_TRUE(open(genuine));
}

TEST_F(SecureChannelTest, RejectsTruncatedPackets) {
    const Bytes packet = seal_control(Bytes{1, 2, 3});
    for (std::size_t length = 0; length < sealed_packet_bytes(0); ++length) {
        const auto opened =
            open(Bytes(packet.begin(), packet.begin() + static_cast<std::ptrdiff_t>(length)));
        ASSERT_FALSE(opened) << "length " << length;
        EXPECT_EQ(opened.error(), TransportError::kTruncated) << "length " << length;
    }
    // 認証タグより長くても、末尾を削れば認証に失敗する。
    const auto shortened = open(Bytes(packet.begin(), packet.end() - 1));
    ASSERT_FALSE(shortened);
    EXPECT_EQ(shortened.error(), TransportError::kAuthenticationFailed);
    EXPECT_TRUE(open(packet));
}

TEST_F(SecureChannelTest, RejectsHandshakePacketsAndOtherConnections) {
    Bytes packet = seal_control({});
    packet[1] = static_cast<std::uint8_t>(PacketType::kHandshakeInitiation);
    EXPECT_EQ(open(packet).error(), TransportError::kUnexpectedPacketType);

    // 別の接続の鍵で作ったパケットは、接続 ID を合わせても認証に失敗する。
    auto [other_client, other_host] = establish();
    Bytes foreign(kMaxPacketBytes);
    const auto written = other_client.sealer.seal_control(Bytes{1}, foreign);
    ASSERT_TRUE(written);
    foreign.resize(written.value());
    EXPECT_EQ(open(foreign).error(), TransportError::kAuthenticationFailed);
}

TEST_F(SecureChannelTest, PacketsAreBoundToDirection) {
    // 子機が送ったパケットを子機自身に送り返しても（反射）、受信鍵が違うので受け付けない。
    const Bytes packet = seal_control(Bytes{1});
    Bytes plaintext(kMaxPacketBytes);
    const auto reflected = client_.opener.open(packet, plaintext);
    ASSERT_FALSE(reflected);
    EXPECT_EQ(reflected.error(), TransportError::kAuthenticationFailed);
}

TEST_F(SecureChannelTest, SealErrorsDoNotConsumeSequenceNumbers) {
    Bytes out(kMaxPacketBytes);
    const Bytes too_large(kMaxPlaintextBytes + 1);
    EXPECT_EQ(client_.sealer.seal_control(too_large, out).error(),
              TransportError::kPayloadTooLarge);

    Bytes small(sealed_packet_bytes(4) - 1);
    EXPECT_EQ(client_.sealer.seal_control(Bytes(4), small).error(),
              TransportError::kBufferTooSmall);

    const MediaPayload empty_media{
        .stream_id = 0, .timestamp = 0, .mute_epoch = 0, .opus_payload = {}};
    EXPECT_EQ(client_.sealer.seal_media(empty_media, out).error(),
              TransportError::kMalformedPayload);

    const Bytes largest(kMaxPlaintextBytes);
    const auto written = client_.sealer.seal_control(largest, out);
    ASSERT_TRUE(written);
    EXPECT_EQ(written.value(), kMaxPacketBytes);
    out.resize(written.value());
    const auto opened = open(out);
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened.value().sequence, 0U);
}

TEST_F(SecureChannelTest, RejectsSmallPlaintextBuffer) {
    const Bytes packet = seal_control(Bytes{1, 2, 3});
    Bytes small(2);
    const auto opened = host_.opener.open(packet, small);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), TransportError::kBufferTooSmall);
    EXPECT_TRUE(open(packet));
}

TEST_F(SecureChannelTest, MovedFromObjectsRefuseToWork) {
    PacketSealer sealer = std::move(client_.sealer);
    PacketOpener opener = std::move(host_.opener);
    Bytes out(kMaxPacketBytes);
    // NOLINTBEGIN(bugprone-use-after-move): ムーブ元が安全に失敗することを確かめる
    EXPECT_EQ(client_.sealer.seal_control({}, out).error(), TransportError::kInvalidState);
    const auto written = sealer.seal_control({}, out);
    ASSERT_TRUE(written);
    out.resize(written.value());
    EXPECT_EQ(host_.opener.open(out, plaintext_).error(), TransportError::kInvalidState);
    // NOLINTEND(bugprone-use-after-move)
    EXPECT_TRUE(opener.open(out, plaintext_));
}

}  // namespace
}  // namespace vocroz::transport
