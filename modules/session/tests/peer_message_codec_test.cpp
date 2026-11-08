// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機と親機の間の制御メッセージのエンコードとデコードのテスト。

#include "wire/peer_message_codec.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::session::wire {
namespace {

using control_protocol::ErrorCode;

template <typename T>
T filled(std::uint8_t value) {
    T out{};
    out.fill(value);
    return out;
}

ClientToHostMessage round_trip(const ClientToHostMessage& message) {
    const auto decoded = decode_client_to_host(encode(message));
    EXPECT_TRUE(decoded);
    return decoded ? decoded.value() : ClientToHostMessage{LeaveNotice{}};
}

HostToClientMessage round_trip(const HostToClientMessage& message) {
    const auto decoded = decode_host_to_client(encode(message));
    EXPECT_TRUE(decoded);
    return decoded ? decoded.value() : HostToClientMessage{SessionEndedNotice{}};
}

TEST(PeerMessageCodecTest, RoundTripsEveryClientToHostMessage) {
    const JoinRequest join{filled<InviteToken>(7), filled<ReconnectToken>(9), "Vo 佐藤"};
    EXPECT_EQ(round_trip(join), ClientToHostMessage{join});
    const JoinRequest fresh{filled<InviteToken>(7), std::nullopt, ""};
    EXPECT_EQ(round_trip(fresh), ClientToHostMessage{fresh});
    const ReconnectRequest reconnect{filled<ReconnectToken>(3)};
    EXPECT_EQ(round_trip(reconnect), ClientToHostMessage{reconnect});
    const SendMuteReport mute{false, 0x01020304};
    EXPECT_EQ(round_trip(mute), ClientToHostMessage{mute});
    EXPECT_EQ(round_trip(InputReport{true}), ClientToHostMessage{InputReport{true}});
    EXPECT_EQ(round_trip(LeaveNotice{}), ClientToHostMessage{LeaveNotice{}});
}

TEST(PeerMessageCodecTest, RoundTripsEveryHostToClientMessage) {
    EXPECT_EQ(round_trip(JoinPending{"日曜リハーサル"}),
              HostToClientMessage{JoinPending{"日曜リハーサル"}});
    const JoinApproved approved{filled<ReconnectToken>(5)};
    EXPECT_EQ(round_trip(approved), HostToClientMessage{approved});
    for (const auto reason :
         {ErrorCode::kRejected, ErrorCode::kInviteExpired, ErrorCode::kInviteRevoked,
          ErrorCode::kInviteInvalid, ErrorCode::kReconnectTimeout, ErrorCode::kSessionEnded}) {
        EXPECT_EQ(round_trip(JoinDenied{reason}), HostToClientMessage{JoinDenied{reason}});
    }
    EXPECT_EQ(round_trip(SessionEndedNotice{}), HostToClientMessage{SessionEndedNotice{}});
}

TEST(PeerMessageCodecTest, EncodesSendMuteInBigEndian) {
    EXPECT_EQ(encode(ClientToHostMessage{SendMuteReport{true, 0x01020304}}),
              (std::vector<std::uint8_t>{0x03, 0x01, 0x01, 0x02, 0x03, 0x04}));
}

TEST(PeerMessageCodecTest, HostRejectsMessagesMeantForTheClientAndUnknownTypes) {
    // 改変された子機が親機向けでない種別や未知の種別（ミキサーの操作など）を送っても受け付けない。
    const auto approved = encode(HostToClientMessage{JoinApproved{filled<ReconnectToken>(1)}});
    EXPECT_EQ(decode_client_to_host(approved).error(), PeerMessageError::kUnknownType);
    const std::vector<std::uint8_t> master_gain{0x10, 0x00, 0x00};
    EXPECT_EQ(decode_client_to_host(master_gain).error(), PeerMessageError::kUnknownType);
    const auto leave = encode(ClientToHostMessage{LeaveNotice{}});
    EXPECT_EQ(decode_host_to_client(leave).error(), PeerMessageError::kUnknownType);
}

TEST(PeerMessageCodecTest, RejectsTruncatedOrTrailingBytes) {
    auto mute = encode(ClientToHostMessage{SendMuteReport{false, 1}});
    mute.pop_back();
    EXPECT_EQ(decode_client_to_host(mute).error(), PeerMessageError::kMalformed);
    auto leave = encode(ClientToHostMessage{LeaveNotice{}});
    leave.push_back(0);
    EXPECT_EQ(decode_client_to_host(leave).error(), PeerMessageError::kMalformed);
    EXPECT_EQ(decode_client_to_host({}).error(), PeerMessageError::kMalformed);
}

TEST(PeerMessageCodecTest, RejectsBooleansOtherThanZeroOrOne) {
    const std::vector<std::uint8_t> input{0x04, 0x02};
    EXPECT_EQ(decode_client_to_host(input).error(), PeerMessageError::kMalformed);
}

TEST(PeerMessageCodecTest, RejectsDisplayNamesThatAreNotDisplayableUtf8) {
    auto bytes =
        encode(ClientToHostMessage{JoinRequest{filled<InviteToken>(1), std::nullopt, "ab"}});
    bytes.back() = '\n';
    EXPECT_EQ(decode_client_to_host(bytes).error(), PeerMessageError::kMalformed);
}

TEST(PeerMessageCodecTest, RejectsUnknownDenyReasons) {
    const std::vector<std::uint8_t> input{0x83, 0x63};
    EXPECT_EQ(decode_host_to_client(input).error(), PeerMessageError::kMalformed);
}

}  // namespace
}  // namespace vocroz::session::wire
