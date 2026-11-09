// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のパケットの受信の処理（HostReceivePath）のテスト。ハンドシェイクの応答を受け箱へ渡すこと、
// 接続を渡した後のパケットを受信スレッドのまま復号すること、返送のミュートの世代で古いフレームを
// 捨てること、接続を付け替えるとタイムスタンプが 0 に戻っても再生できること、送信元の変化と
// 置き場の片づけを、1 つのスレッドで確かめる。

#include "host_receive_path.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/noise_ik_handshake.h"

#include "return_frames.h"
#include "secure_pair.h"

namespace vocroz::client {
namespace {

namespace tp = transport;

const network::UdpAddress kHost = network::UdpAddress::from_ipv4({203, 0, 113, 9}, 50000);
const network::UdpAddress kMovedHost = network::UdpAddress::from_ipv4({203, 0, 113, 9}, 50001);

class HostReceivePathTest : public ::testing::Test {
protected:
    // 親機側の暗号化と、子機側の復号器を渡した接続を 1 つ作る。
    tp::PacketSealer attach(std::uint32_t generation) {
        auto pair = test::make_secure_pair();
        EXPECT_TRUE(path_.attach(generation, std::move(pair.client.opener), kHost));
        return std::move(pair.host.sealer);
    }

    void send_control(tp::PacketSealer& sealer, std::vector<std::uint8_t> plaintext,
                      const network::UdpAddress& from = kHost) {
        std::vector<std::uint8_t> packet(tp::kMaxPacketBytes);
        const auto sealed = sealer.seal_control(plaintext, packet).value();
        path_.on_datagram(from, std::span(packet).first(sealed));
    }

    void send_frames(tp::PacketSealer& sealer, std::uint32_t epoch, int count,
                     std::uint16_t stream_id = tp::kReturnStreamId) {
        std::vector<std::uint8_t> packet(tp::kMaxPacketBytes);
        for (int i = 0; i < count; ++i) {
            const auto sealed = sealer
                                    .seal_media(tp::MediaPayload{stream_id,
                                                                 static_cast<std::uint64_t>(i) *
                                                                     test::kReturnFrameSamples,
                                                                 epoch, opus_},
                                                packet)
                                    .value();
            path_.on_datagram(kHost, std::span(packet).first(sealed));
        }
    }

    // オーディオスレッドの代わりに返送を描画し、デコードしたフレームの数を返す。
    std::uint64_t render_decoded(std::size_t samples = 2400) {
        std::vector<float> out(samples);
        returns_->render(out);
        return returns_->stats().frames_decoded;
    }

    std::vector<HostPacketEvent> take_events() {
        std::vector<HostPacketEvent> events;
        while (auto event = path_.pop_event()) {
            events.push_back(*event);
        }
        return events;
    }

    PeerInbox inbox_;
    std::unique_ptr<tp::PlayoutStream> returns_ =
        tp::PlayoutStream::create(tp::PlayoutConfig{}).value();
    HostReceivePath path_{inbox_, *returns_};
    std::vector<std::uint8_t> opus_ = test::encode_return_frame();
};

TEST_F(HostReceivePathTest, HandsHandshakeResponsesToTheInbox) {
    const auto client_keys = test::make_keys();
    const auto host_keys = test::make_keys();
    auto initiator =
        tp::NoiseIkHandshake::create_initiator(client_keys, host_keys.public_key(), 0x55).value();
    auto responder = tp::NoiseIkHandshake::create_responder(host_keys).value();
    std::vector<std::uint8_t> packet(tp::kMaxPacketBytes);
    std::vector<std::uint8_t> payload(tp::kMaxPacketBytes);
    const auto first = initiator.write_initiation({}, packet).value();
    static_cast<void>(responder.read_initiation(std::span(packet).first(first), payload).value());
    const auto response = responder.write_response({}, packet).value();

    path_.on_datagram(kHost, std::span(packet).first(response));
    const auto popped = inbox_.pop();
    ASSERT_TRUE(popped);
    EXPECT_EQ(popped->size, response);
    EXPECT_EQ(popped->from, kHost);
}

TEST_F(HostReceivePathTest, DecryptsControlPacketsOfTheLinkForTheControlThread) {
    auto sealer = attach(1);
    send_control(sealer, {1, 2, 3});
    const auto events = take_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, HostPacketEvent::Kind::kControl);
    EXPECT_EQ(events[0].generation, 1U);
    EXPECT_EQ(std::vector(events[0].view().begin(), events[0].view().end()),
              (std::vector<std::uint8_t>{1, 2, 3}));
    EXPECT_EQ(path_.authenticated_packets(), 1U);
    EXPECT_FALSE(inbox_.pop());
}

TEST_F(HostReceivePathTest, DropsPacketsOfAnUnknownLink) {
    auto pair = test::make_secure_pair();
    send_control(pair.host.sealer, {1});
    EXPECT_TRUE(take_events().empty());
    EXPECT_EQ(path_.authenticated_packets(), 0U);
}

TEST_F(HostReceivePathTest, PlaysTheReturnAfterTheHostUnmutes) {
    auto sealer = attach(1);
    ASSERT_TRUE(path_.set_return_mute(1, false, 1));
    send_frames(sealer, 1, 8);
    EXPECT_GT(render_decoded(), 0U);
}

TEST_F(HostReceivePathTest, DropsFramesOfAMutedEpoch) {
    auto sealer = attach(1);
    ASSERT_TRUE(path_.set_return_mute(1, false, 1));
    ASSERT_TRUE(path_.set_return_mute(1, true, 1));
    send_frames(sealer, 1, 8);
    EXPECT_EQ(render_decoded(), 0U);
    // ミュートの前（解除される前の世代 0）のフレームも捨てる。
    send_frames(sealer, 0, 8);
    EXPECT_EQ(render_decoded(), 0U);
}

TEST_F(HostReceivePathTest, IgnoresReturnMuteOfAnOldLink) {
    auto sealer = attach(2);
    ASSERT_TRUE(path_.set_return_mute(2, false, 1));
    // 前の接続で届いたミュートは、今の接続の返送を止めない。
    ASSERT_TRUE(path_.set_return_mute(1, true, 1));
    send_frames(sealer, 1, 8);
    EXPECT_GT(render_decoded(), 0U);
}

TEST_F(HostReceivePathTest, IgnoresStreamsOtherThanTheReturn) {
    auto sealer = attach(1);
    ASSERT_TRUE(path_.set_return_mute(1, false, 1));
    send_frames(sealer, 1, 8, 1);
    EXPECT_EQ(render_decoded(), 0U);
}

TEST_F(HostReceivePathTest, PlaysANewLinkWhoseTimestampsStartAgainWithTheSameEpoch) {
    auto first = attach(1);
    ASSERT_TRUE(path_.set_return_mute(1, false, 3));
    send_frames(first, 3, 8);
    const auto before = render_decoded();
    ASSERT_GT(before, 0U);

    // 再接続。親機の世代番号は続くが、タイムスタンプは 0 から始まる。
    auto second = attach(2);
    ASSERT_TRUE(path_.set_return_mute(2, false, 3));
    send_frames(second, 3, 8);
    EXPECT_GT(render_decoded(), before);
    EXPECT_EQ(returns_->stats().frames_late, 0U);
}

TEST_F(HostReceivePathTest, ReportsANewSourceAddressOnce) {
    auto sealer = attach(1);
    send_control(sealer, {1}, kMovedHost);
    send_control(sealer, {2}, kMovedHost);
    const auto events = take_events();
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].kind, HostPacketEvent::Kind::kRemoteChanged);
    EXPECT_EQ(events[0].from, kMovedHost);
    EXPECT_EQ(events[1].kind, HostPacketEvent::Kind::kControl);
    EXPECT_EQ(events[2].kind, HostPacketEvent::Kind::kControl);
}

TEST_F(HostReceivePathTest, DetachedLinkStopsAndItsSlotIsReused) {
    // 置き場の数より多く付け替えても、手放した置き場を片づけて使い回せる。
    for (std::uint32_t generation = 1; generation <= kHostOpenerSlots * 3; ++generation) {
        auto sealer = attach(generation);
        send_control(sealer, {1});
        ASSERT_TRUE(path_.detach(generation));
        send_control(sealer, {2});
        const auto events = take_events();
        ASSERT_EQ(events.size(), 1U) << generation;
        EXPECT_EQ(events[0].view()[0], 1U);
    }
}

}  // namespace
}  // namespace vocroz::client
