// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーメッセージのシリアライズとパースのテスト。
// 期待値のバイト列は、サーバーの実装（infra/rendezvous/internal/udpapi/wire.go）の Append が
// 同じ値から生成したもので、クライアントとサーバーで形式が一致することを確かめる。

#include "wire/rendezvous_codec.h"

#include <array>

#include <gtest/gtest.h>

#include "hex_bytes.h"

namespace vocroz::network::wire {
namespace {

using test::hex_bytes;

constexpr TxId kTxId = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7};

RendezvousId sequential_id(std::uint8_t first) {
    RendezvousId id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>(first + i);
    }
    return id;
}

UdpAddress ipv6(std::initializer_list<std::uint8_t> head, std::uint8_t last, std::uint16_t port) {
    Ipv6Bytes ip{};
    std::copy(head.begin(), head.end(), ip.begin());
    ip[15] = last;
    return UdpAddress::from_ipv6(ip, port);
}

TEST(RendezvousCodecTest, EncodesRegisterLikeTheServer) {
    const std::array candidates = {UdpAddress::from_ipv4({192, 168, 1, 10}, 40000),
                                   ipv6({0x20, 0x01, 0x0d, 0xb8}, 0x09, 40000)};
    const RegisterMessage message{kTxId, sequential_id(0x01), sequential_id(0x10),
                                  RendezvousRole::kClient, candidates};
    std::array<std::uint8_t, kMaxRegisterBytes> out{};
    const auto written = encode_register(message, out);
    ASSERT_TRUE(written);
    const auto expected = hex_bytes(
        "56525a0101a0a1a2a3a4a5a6a70102030405060708090a0b0c0d0e0f10101112131415161718191a1b1c1d1e1f"
        "0202049c40c0a8010a069c4020010db8000000000000000000000009");
    EXPECT_EQ(std::vector<std::uint8_t>(out.begin(), out.begin() + written.value()), expected);
}

TEST(RendezvousCodecTest, RejectsRegisterWithUnroutableCandidate) {
    const std::array candidates = {UdpAddress::from_ipv4({192, 168, 1, 10}, 0)};
    const RegisterMessage message{kTxId, {}, {}, RendezvousRole::kHost, candidates};
    std::array<std::uint8_t, kMaxRegisterBytes> out{};
    EXPECT_FALSE(encode_register(message, out));
}

TEST(RendezvousCodecTest, RejectsRegisterWithTooManyCandidates) {
    std::vector<UdpAddress> candidates(kMaxLocalCandidates + 1,
                                       UdpAddress::from_ipv4({10, 0, 0, 1}, 1000));
    const RegisterMessage message{kTxId, {}, {}, RendezvousRole::kHost, candidates};
    std::array<std::uint8_t, kMaxRegisterBytes + 64> out{};
    EXPECT_FALSE(encode_register(message, out));
}

TEST(RendezvousCodecTest, ParsesRegisteredFromTheServer) {
    const auto bytes = hex_bytes("56525a0102a0a1a2a3a4a5a6a7000104c350cb007107");
    ASSERT_TRUE(is_rendezvous_message(bytes));
    const auto parsed = parse_registered(bytes);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().tx_id, kTxId);
    EXPECT_FALSE(parsed.value().capacity_exceeded);
    EXPECT_TRUE(parsed.value().peer_present);
    EXPECT_EQ(parsed.value().reflexive, UdpAddress::from_ipv4({203, 0, 113, 7}, 50000));
}

TEST(RendezvousCodecTest, ParsesCapacityStatus) {
    auto bytes = hex_bytes("56525a0102a0a1a2a3a4a5a6a7010004c350cb007107");
    const auto parsed = parse_registered(bytes);
    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed.value().capacity_exceeded);
    EXPECT_FALSE(parsed.value().peer_present);
}

TEST(RendezvousCodecTest, RejectsRegisteredWithUnknownStatusOrTrailingBytes) {
    EXPECT_FALSE(parse_registered(hex_bytes("56525a0102a0a1a2a3a4a5a6a7020104c350cb007107")));
    EXPECT_FALSE(parse_registered(hex_bytes("56525a0102a0a1a2a3a4a5a6a7000104c350cb00710700")));
    EXPECT_FALSE(parse_registered(hex_bytes("56525a0102a0a1a2a3a4a5a6a7000104c350cb0071")));
}

TEST(RendezvousCodecTest, RejectsWrongVersionAndType) {
    // バージョン 2。
    EXPECT_FALSE(parse_registered(hex_bytes("56525a0202a0a1a2a3a4a5a6a7000104c350cb007107")));
    // REGISTERED を PUNCH としてパースする。
    EXPECT_FALSE(parse_punch(hex_bytes("56525a0102a0a1a2a3a4a5a6a7000104c350cb007107")));
}

TEST(RendezvousCodecTest, ParsesPunchFromTheServer) {
    const auto bytes = hex_bytes(
        "56525a0103a0a1a2a3a4a5a6a70102030405060708090a0b0c0d0e0f10101112131415161718191a1b1c1d1e1f"
        "0102049c41c6336402069c4120010db8000000000000000000000005");
    const auto parsed = parse_punch(bytes);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().tx_id, kTxId);
    EXPECT_EQ(parsed.value().rendezvous_id, sequential_id(0x01));
    EXPECT_EQ(parsed.value().peer_id, sequential_id(0x10));
    EXPECT_EQ(parsed.value().peer_role, RendezvousRole::kHost);
    ASSERT_EQ(parsed.value().candidates.size(), 2u);
    EXPECT_EQ(parsed.value().candidates[0], UdpAddress::from_ipv4({198, 51, 100, 2}, 40001));
    EXPECT_EQ(parsed.value().candidates[1], ipv6({0x20, 0x01, 0x0d, 0xb8}, 0x05, 40001));
}

TEST(RendezvousCodecTest, RejectsPunchWithUnspecifiedOrMulticastCandidate) {
    // 候補が 0.0.0.0:40001。
    EXPECT_FALSE(parse_punch(hex_bytes(
        "56525a0103a0a1a2a3a4a5a6a70102030405060708090a0b0c0d0e0f10101112131415161718191a1b1c1d1e1f"
        "0101049c4100000000")));
    // 候補が 224.0.0.1:40001。
    EXPECT_FALSE(parse_punch(hex_bytes(
        "56525a0103a0a1a2a3a4a5a6a70102030405060708090a0b0c0d0e0f10101112131415161718191a1b1c1d1e1f"
        "0101049c41e0000001")));
    // 役割が 3。
    EXPECT_FALSE(parse_punch(hex_bytes(
        "56525a0103a0a1a2a3a4a5a6a70102030405060708090a0b0c0d0e0f10101112131415161718191a1b1c1d1e1f"
        "0301049c41c6336402")));
}

TEST(RendezvousCodecTest, TransportPacketsAreNotRendezvousMessages) {
    // transport のパケットの先頭はプロトコルバージョン 1。
    const std::vector<std::uint8_t> packet(32, 0x01);
    EXPECT_FALSE(is_rendezvous_message(packet));
    // "VRZ" で始まっても、ヘッダーより短ければランデブーメッセージとして扱わない。
    EXPECT_FALSE(is_rendezvous_message(hex_bytes("56525a01")));
}

}  // namespace
}  // namespace vocroz::network::wire
