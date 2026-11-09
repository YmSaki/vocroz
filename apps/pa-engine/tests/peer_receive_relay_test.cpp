// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerReceiveRelay の単体テスト。通知がループに積まれ、引数を写したうえでループのスレッドで
// 受け取り手へ届くことを確かめる。

#include "peer_receive_relay.h"

#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using namespace std::chrono_literals;

class RecordingListener final : public PeerReceiveListener {
public:
    void on_handshake_initiation(const network::UdpAddress& from,
                                 std::span<const std::uint8_t> datagram) override {
        log.push_back("handshake:" + std::to_string(from.port) + ":" +
                      std::to_string(datagram.size()));
        bytes.assign(datagram.begin(), datagram.end());
    }
    void on_control_packet(session::PeerHandle peer,
                           std::span<const std::uint8_t> plaintext) override {
        log.push_back("control:" + std::to_string(peer) + ":" + std::to_string(plaintext.size()));
    }
    void on_signal_state(session::PeerHandle peer, transport::SignalState state) override {
        log.push_back("signal:" + std::to_string(peer) + ":" +
                      std::to_string(static_cast<int>(state)));
    }
    void on_receive_released(session::PeerHandle peer) override {
        log.push_back("released:" + std::to_string(peer));
    }

    std::vector<std::string> log;
    std::vector<std::uint8_t> bytes;
};

TEST(PeerReceiveRelayTest, DeliversCopiesOnTheLoop) {
    ControlLoop loop;
    RecordingListener target;
    PeerReceiveRelay relay(loop);
    relay.set_target(&target);
    {
        std::vector<std::uint8_t> datagram{1, 2, 3};
        relay.on_handshake_initiation(network::UdpAddress::from_ipv4({10, 0, 0, 1}, 5000),
                                      datagram);
        // 呼び出しのあとで元の領域が変わっても、写したものが届く。
        datagram.assign(3, 0);
    }
    relay.on_control_packet(2, std::vector<std::uint8_t>{4, 5});
    relay.on_signal_state(2, transport::SignalState::kNoSignal);
    relay.on_receive_released(2);
    EXPECT_TRUE(target.log.empty());
    loop.post([&] { loop.request_stop(); });
    loop.run(1000ms, [] {});
    EXPECT_EQ(target.log,
              (std::vector<std::string>{
                  "handshake:5000:3", "control:2:2",
                  "signal:2:" + std::to_string(static_cast<int>(transport::SignalState::kNoSignal)),
                  "released:2"}));
    EXPECT_EQ(target.bytes, (std::vector<std::uint8_t>{1, 2, 3}));
}

TEST(PeerReceiveRelayTest, DropsNotificationsWithoutTarget) {
    ControlLoop loop;
    PeerReceiveRelay relay(loop);
    relay.on_receive_released(1);
    bool ran_other = false;
    loop.post([&] {
        ran_other = true;
        loop.request_stop();
    });
    loop.run(1000ms, [] {});
    EXPECT_TRUE(ran_other);
}

}  // namespace
}  // namespace vocroz::engine
