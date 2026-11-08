// SPDX-License-Identifier: AGPL-3.0-only
//
// ReliableControlChannel の単体テスト。
// 2
// つの端点の間の経路をテストが受け持ち、パケットの欠落、重複、順序の入れ替わりを決定的に起こして、
// 再送、重複の排除、送信順の受け取り、キープアライブ、窓の上限を FakeClock で確かめる。

#include "vocroz/transport/reliable_control_channel.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/ports/control_message_listener.h"

#include "fakes/fake_clock.h"
#include "fakes/fake_control_packet_sink.h"

namespace vocroz::transport {
namespace {

using std::chrono::milliseconds;
using Bytes = std::vector<std::uint8_t>;

// 受け取ったメッセージを記録する。on_receive が設定されていれば、記録した後に呼ぶ。
class RecordingListener final : public ControlMessageListener {
public:
    void on_control_message(ControlLane lane, std::span<const std::uint8_t> message) override {
        received.emplace_back(lane, Bytes(message.begin(), message.end()));
        if (on_receive) {
            on_receive(lane, message);
        }
    }

    std::vector<std::pair<ControlLane, Bytes>> received;
    std::function<void(ControlLane, std::span<const std::uint8_t>)> on_receive;
};

// 時刻、送信の記録、受け取りの記録を持つ 1 つの端点。
struct Endpoint {
    explicit Endpoint(const common::FakeClock& clock) : channel(clock, sink, listener) {}

    FakeControlPacketSink sink;
    RecordingListener listener;
    ReliableControlChannel channel;
};

Bytes message(std::uint8_t tag, std::size_t size = 3) {
    return Bytes(size, tag);
}

class ReliableControlChannelTest : public ::testing::Test {
protected:
    // from が送ったパケットをすべて to に届ける。
    static void deliver_all(Endpoint& from, Endpoint& to) {
        for (const auto& packet : from.sink.take()) {
            ASSERT_TRUE(to.channel.on_control_packet(packet));
        }
    }

    // 子機と親機の間で、送るものがなくなるまでパケットを往復させる。
    void exchange() {
        while (!client_.sink.packets.empty() || !host_.sink.packets.empty()) {
            deliver_all(client_, host_);
            deliver_all(host_, client_);
        }
    }

    // 時刻を進め、両端の tick() を呼ぶ。
    void advance(common::Clock::TimePoint::duration delta) {
        clock_.advance(delta);
        client_.channel.tick();
        host_.channel.tick();
    }

    common::FakeClock clock_;
    Endpoint client_{clock_};
    Endpoint host_{clock_};
};

TEST_F(ReliableControlChannelTest, SendsImmediatelyAndPeerAcksImmediately) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    ASSERT_EQ(client_.sink.packets.size(), 1U);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 1U);

    deliver_all(client_, host_);
    ASSERT_EQ(host_.listener.received.size(), 1U);
    EXPECT_EQ(host_.listener.received[0].first, ControlLane::kControl);
    EXPECT_EQ(host_.listener.received[0].second, message(1));
    // ACK はタイマーを待たずに、受け取ったその場で返す。
    ASSERT_EQ(host_.sink.packets.size(), 1U);
    EXPECT_EQ(host_.sink.packets[0].size(), kAckFrameBytes);

    deliver_all(host_, client_);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 0U);
    // ACK だけのパケットには ACK を返さない（ACK の応酬を続けないため）。
    EXPECT_TRUE(client_.sink.packets.empty());
}

TEST_F(ReliableControlChannelTest, RetransmitsLostMessageAfterInitialTimeout) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    static_cast<void>(client_.sink.take());  // 欠落させる

    advance(milliseconds{249});
    EXPECT_TRUE(client_.sink.packets.empty());
    advance(milliseconds{1});
    ASSERT_EQ(client_.sink.packets.size(), 1U);

    exchange();
    ASSERT_EQ(host_.listener.received.size(), 1U);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 0U);
}

TEST_F(ReliableControlChannelTest, DoublesRetransmitTimeoutUpToOneSecond) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    static_cast<void>(client_.sink.take());

    // 250ms → 500ms → 1000ms → 1000ms（上限）の間隔で再送する。
    for (const auto interval :
         {milliseconds{250}, milliseconds{500}, milliseconds{1000}, milliseconds{1000}}) {
        EXPECT_EQ(client_.channel.next_deadline(), clock_.now() + interval);
        advance(interval - milliseconds{1});
        EXPECT_TRUE(client_.sink.packets.empty());
        advance(milliseconds{1});
        EXPECT_EQ(client_.sink.take().size(), 1U);
    }
}

TEST_F(ReliableControlChannelTest, DeliversDuplicatedPacketOnlyOnceButAcksEachTime) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    const auto packet = client_.sink.take().at(0);

    ASSERT_TRUE(host_.channel.on_control_packet(packet));
    ASSERT_TRUE(host_.channel.on_control_packet(packet));

    EXPECT_EQ(host_.listener.received.size(), 1U);
    // 前の ACK が失われていても送信側が再送をやめられるよう、重複にも ACK を返す。
    EXPECT_EQ(host_.sink.packets.size(), 2U);
}

TEST_F(ReliableControlChannelTest, DeliversInSendOrderWhenPacketsAreReordered) {
    for (std::uint8_t tag = 0; tag < 3; ++tag) {
        ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(tag)));
    }
    const auto packets = client_.sink.take();
    ASSERT_EQ(packets.size(), 3U);

    ASSERT_TRUE(host_.channel.on_control_packet(packets[2]));
    ASSERT_TRUE(host_.channel.on_control_packet(packets[1]));
    // 先頭が届くまでは、後のメッセージを渡さない。
    EXPECT_TRUE(host_.listener.received.empty());

    ASSERT_TRUE(host_.channel.on_control_packet(packets[0]));
    ASSERT_EQ(host_.listener.received.size(), 3U);
    for (std::uint8_t tag = 0; tag < 3; ++tag) {
        EXPECT_EQ(host_.listener.received[tag].second, message(tag));
    }
}

TEST_F(ReliableControlChannelTest, RetransmitsWhenAckIsLostAndPeerDiscardsTheRetransmission) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    deliver_all(client_, host_);
    static_cast<void>(host_.sink.take());  // ACK を欠落させる

    advance(milliseconds{250});
    ASSERT_EQ(client_.sink.packets.size(), 1U);
    exchange();

    EXPECT_EQ(host_.listener.received.size(), 1U);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 0U);
}

TEST_F(ReliableControlChannelTest, SelectiveAckLimitsRetransmissionToMissingMessage) {
    for (std::uint8_t tag = 0; tag < 3; ++tag) {
        ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(tag)));
    }
    auto packets = client_.sink.take();
    // 先頭だけを欠落させる。
    ASSERT_TRUE(host_.channel.on_control_packet(packets[1]));
    ASSERT_TRUE(host_.channel.on_control_packet(packets[2]));
    deliver_all(host_, client_);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 3U);

    advance(milliseconds{250});
    const auto retransmitted = client_.sink.take();
    ASSERT_EQ(retransmitted.size(), 1U);
    // 選択 ACK で受信済みとわかった 2 つは再送せず、欠けた 1 つだけを送る。
    EXPECT_EQ(retransmitted[0].size(), kDataFrameHeaderBytes + message(0).size());

    ASSERT_TRUE(host_.channel.on_control_packet(retransmitted[0]));
    ASSERT_EQ(host_.listener.received.size(), 3U);
    EXPECT_EQ(host_.listener.received[0].second, message(0));
    deliver_all(host_, client_);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 0U);
}

TEST_F(ReliableControlChannelTest, LanesDoNotWaitForEachOther) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    static_cast<void>(client_.sink.take());  // 制御レーンのメッセージを欠落させる
    ASSERT_TRUE(client_.channel.send(ControlLane::kMidi, message(2)));

    deliver_all(client_, host_);
    // 制御レーンの再送を待たずに、MIDI のレーンのメッセージを渡す。
    ASSERT_EQ(host_.listener.received.size(), 1U);
    EXPECT_EQ(host_.listener.received[0].first, ControlLane::kMidi);
}

TEST_F(ReliableControlChannelTest, SendsKeepaliveAfterOneSecondWithoutSending) {
    EXPECT_EQ(client_.channel.next_deadline(), clock_.now() + std::chrono::seconds{1});
    advance(milliseconds{999});
    EXPECT_TRUE(client_.sink.packets.empty());
    advance(milliseconds{1});
    ASSERT_EQ(client_.sink.packets.size(), 1U);
    // キープアライブは空の平文。
    EXPECT_TRUE(client_.sink.packets[0].empty());

    // キープアライブを受け取っても、メッセージも ACK も生じない。
    static_cast<void>(host_.sink.take());  // 親機も同じ時刻にキープアライブを送っている
    deliver_all(client_, host_);
    EXPECT_TRUE(host_.listener.received.empty());
    EXPECT_TRUE(host_.sink.packets.empty());
}

TEST_F(ReliableControlChannelTest, SendingPostponesKeepalive) {
    advance(milliseconds{600});
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    exchange();

    advance(milliseconds{999});
    EXPECT_TRUE(client_.sink.packets.empty());
    advance(milliseconds{1});
    EXPECT_EQ(client_.sink.packets.size(), 1U);
}

TEST_F(ReliableControlChannelTest, StopsSendingBeyondWindowUntilAcked) {
    for (int i = 0; i < 65; ++i) {
        ASSERT_TRUE(
            client_.channel.send(ControlLane::kControl, message(static_cast<std::uint8_t>(i))));
    }
    // 窓の 64 個までは送り、65 個目は先頭の ACK を待つ。
    auto packets = client_.sink.take();
    ASSERT_EQ(packets.size(), 64U);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 65U);

    ASSERT_TRUE(host_.channel.on_control_packet(packets[0]));
    deliver_all(host_, client_);
    ASSERT_EQ(client_.sink.packets.size(), 1U);
    EXPECT_EQ(client_.sink.packets[0].size(), kDataFrameHeaderBytes + message(64).size());
}

TEST_F(ReliableControlChannelTest, RejectsMessageLargerThanLimit) {
    EXPECT_TRUE(client_.channel.send(ControlLane::kControl, message(1, kMaxControlMessageBytes)));
    const auto too_large =
        client_.channel.send(ControlLane::kControl, message(1, kMaxControlMessageBytes + 1));
    ASSERT_FALSE(too_large);
    EXPECT_EQ(too_large.error(), TransportError::kPayloadTooLarge);
}

TEST_F(ReliableControlChannelTest, RejectsSendWhenQueueIsFull) {
    for (int i = 0; i < 256; ++i) {
        ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    }
    const auto full = client_.channel.send(ControlLane::kControl, message(1));
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error(), TransportError::kQueueFull);
    // 上限はレーンごとなので、MIDI のレーンには送れる。
    EXPECT_TRUE(client_.channel.send(ControlLane::kMidi, message(1)));
}

TEST_F(ReliableControlChannelTest, LargestMessageFitsTogetherWithAcksOfBothLanes) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    ASSERT_TRUE(client_.channel.send(ControlLane::kMidi, message(2)));
    auto packets = client_.sink.take();
    ASSERT_EQ(packets.size(), 2U);
    // 2 つのデータフレームを 1 つの平文にまとめて届け、両レーンの ACK が溜まった状態で
    // 最大長のメッセージを送らせる。
    Bytes combined = packets[0];
    combined.insert(combined.end(), packets[1].begin(), packets[1].end());
    host_.listener.on_receive = [this](ControlLane lane, std::span<const std::uint8_t>) {
        if (lane == ControlLane::kMidi) {
            EXPECT_TRUE(
                host_.channel.send(ControlLane::kControl, message(3, kMaxControlMessageBytes)));
        }
    };
    ASSERT_TRUE(host_.channel.on_control_packet(combined));

    ASSERT_EQ(host_.sink.packets.size(), 1U);
    EXPECT_EQ(host_.sink.packets[0].size(),
              2 * kAckFrameBytes + kDataFrameHeaderBytes + kMaxControlMessageBytes);
    deliver_all(host_, client_);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 0U);
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kMidi), 0U);
    ASSERT_EQ(client_.listener.received.size(), 1U);
}

TEST_F(ReliableControlChannelTest, ShortensRetransmitTimeoutFromMeasuredRtt) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    clock_.advance(milliseconds{20});
    exchange();  // RTT 20ms を測る

    // 最初の測定値では平滑化 RTT 20ms、変動 10ms なので、再送タイムアウトは 20 + 4 * 10 = 60ms。
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(2)));
    static_cast<void>(client_.sink.take());
    advance(milliseconds{59});
    EXPECT_TRUE(client_.sink.packets.empty());
    advance(milliseconds{1});
    EXPECT_EQ(client_.sink.packets.size(), 1U);
}

TEST_F(ReliableControlChannelTest, ListenerCanReplyFromCallback) {
    host_.listener.on_receive = [this](ControlLane lane, std::span<const std::uint8_t> received) {
        EXPECT_TRUE(host_.channel.send(lane, received));
    };
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(7)));
    exchange();

    ASSERT_EQ(client_.listener.received.size(), 1U);
    EXPECT_EQ(client_.listener.received[0].second, message(7));
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 0U);
    EXPECT_EQ(host_.channel.unacknowledged_count(ControlLane::kControl), 0U);
}

TEST_F(ReliableControlChannelTest, RecoversFromSinkFailureByRetransmitting) {
    client_.sink.fail_sends = true;
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    client_.sink.fail_sends = false;

    advance(milliseconds{250});
    exchange();
    EXPECT_EQ(host_.listener.received.size(), 1U);
}

// 不正な平文は状態を変えずに kMalformedPayload で拒む。
class ReliableControlChannelMalformedTest : public ReliableControlChannelTest,
                                            public ::testing::WithParamInterface<Bytes> {};

TEST_P(ReliableControlChannelMalformedTest, RejectsWithoutDeliveringOrAcking) {
    ASSERT_TRUE(client_.channel.send(ControlLane::kControl, message(1)));
    static_cast<void>(client_.sink.take());

    const auto result = client_.channel.on_control_packet(GetParam());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), TransportError::kMalformedPayload);
    EXPECT_TRUE(client_.listener.received.empty());
    EXPECT_TRUE(client_.sink.packets.empty());
    EXPECT_EQ(client_.channel.unacknowledged_count(ControlLane::kControl), 1U);
}

INSTANTIATE_TEST_SUITE_P(MalformedPlaintexts, ReliableControlChannelMalformedTest,
                         ::testing::Values(
                             // 未知のフレーム種別
                             Bytes{0x09, 0x00},
                             // 種別だけで途中で切れている
                             Bytes{0x02},
                             // 未知のレーン
                             Bytes{0x02, 0x05, 0, 0, 0, 0, 0, 0},
                             // ACK フレームが途中で切れている
                             Bytes{0x01, 0x00, 0, 0, 0, 1},
                             // データフレームの長さが残りより長い
                             Bytes{0x02, 0x00, 0, 0, 0, 0, 0, 4, 0xAA},
                             // 送ったことのない連番（1）まで受信済みとする累積 ACK
                             Bytes{0x01, 0x00, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0},
                             // 送ったことのない連番（1）を受信済みとする選択 ACK
                             Bytes{0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
                             // 正しいデータフレームの後に不正なフレーム。前のフレームも適用しない
                             Bytes{0x02, 0x00, 0, 0, 0, 0, 0, 1, 0xAA, 0x07}));

}  // namespace
}  // namespace vocroz::transport
