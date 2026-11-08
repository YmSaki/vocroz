// SPDX-License-Identifier: AGPL-3.0-only
//
// 悪条件の回線での、制御メッセージの結合テスト。
// 信頼制御層（ReliableControlChannel）を暗号化（SecureControlPacketSink、PacketOpener）と
// シミュレーションのネットワークにつなぎ、両方向に欠損、重複、順序の入れ替え、ジッターがあっても、
// 承認やミュートの通知に使う制御メッセージがすべて、1 回ずつ、送った順に届くことを確かめる。

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/netsim/simulated_network.h"
#include "vocroz/test_harness/secure_link.h"
#include "vocroz/test_harness/simulated_datagram_socket.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/reliable_control_channel.h"
#include "vocroz/transport/secure_control_packet_sink.h"

#include "fakes/fake_clock.h"

namespace vocroz::test_harness {
namespace {

using std::chrono::milliseconds;
using transport::ControlLane;

constexpr netsim::NodeAddress kClient{.host = 2, .port = 50000};
constexpr netsim::NodeAddress kHost{.host = 1, .port = 40000};

class RecordingListener final : public transport::ControlMessageListener {
public:
    void on_control_message(ControlLane lane, std::span<const std::uint8_t> message) override {
        received.emplace_back(lane, std::vector<std::uint8_t>(message.begin(), message.end()));
    }

    std::vector<std::pair<ControlLane, std::vector<std::uint8_t>>> received;
};

// 1 つの端点（送信の暗号化、ソケット、信頼制御層、受信の復号）。
struct Endpoint {
    Endpoint(const common::Clock& clock, netsim::SimulatedNetwork& network,
             transport::SecureChannel secure, const netsim::NodeAddress& local,
             const netsim::NodeAddress& remote)
        : channel_(std::move(secure)),
          socket(network, local, remote),
          sink(channel_.sealer, socket),
          control(clock, sink, listener) {
        network.attach(local,
                       [this](const netsim::NodeAddress&, std::span<const std::uint8_t> data) {
                           const auto opened = channel_.opener.open(data, plaintext);
                           if (!opened) {
                               ++rejected;
                               return;
                           }
                           EXPECT_TRUE(control.on_control_packet(opened.value().plaintext));
                       });
    }

    transport::SecureChannel channel_;
    SimulatedDatagramSocket socket;
    transport::SecureControlPacketSink sink;
    RecordingListener listener;
    transport::ReliableControlChannel control;
    std::vector<std::uint8_t> plaintext = std::vector<std::uint8_t>(transport::kMaxPacketBytes);
    std::uint64_t rejected = 0;
};

TEST(ControlFaultsTest, DeliversEveryMessageOnceInOrderOverLossyLink) {
    common::FakeClock clock;
    netsim::SimulatedNetwork network(clock);
    auto link = establish_secure_link(0xC0FFEE);
    ASSERT_TRUE(link);
    Endpoint client(clock, network, std::move(link.value().client), kClient, kHost);
    Endpoint host(clock, network, std::move(link.value().host), kHost, kClient);

    netsim::FaultProfile faults;
    faults.delay = milliseconds{20};
    faults.jitter = milliseconds{10};
    faults.loss = 0.2;
    faults.duplicate = 0.1;
    faults.reorder = 0.1;
    faults.seed = 4;
    network.set_fault_profile(kClient, kHost, faults);
    faults.seed = 5;
    network.set_fault_profile(kHost, kClient, faults);

    constexpr std::uint8_t kMessages = 200;
    std::uint8_t sent = 0;
    // 10ms ごとに 1 つ送り、すべて送ってから 30 秒の間に届けばよい。
    for (int step = 0; step < 32'000; ++step) {
        if (step % 10 == 0 && sent < kMessages) {
            const std::uint8_t message[] = {sent, static_cast<std::uint8_t>(~sent)};
            if (client.control.send(ControlLane::kControl, message)) {
                ++sent;
            }
        }
        clock.advance(milliseconds{1});
        network.deliver_due();
        client.control.tick();
        host.control.tick();
    }

    ASSERT_EQ(sent, kMessages);
    ASSERT_EQ(host.listener.received.size(), kMessages);
    for (std::uint8_t i = 0; i < kMessages; ++i) {
        EXPECT_EQ(host.listener.received[i].first, ControlLane::kControl);
        EXPECT_EQ(host.listener.received[i].second,
                  (std::vector<std::uint8_t>{i, static_cast<std::uint8_t>(~i)}));
    }
    EXPECT_EQ(client.control.unacknowledged_count(ControlLane::kControl), 0U);
    // 経路の重複はリプレイ排除で捨てる。改ざんはしていないので、それ以外の理由では捨てない。
    EXPECT_GT(host.rejected, 0U);
}

}  // namespace
}  // namespace vocroz::test_harness
