// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機から親機への接続（HostConnection）のテスト。ランデブーサーバーの REGISTERED と PUNCH を
// 渡し、親機の候補へ送った鍵交換に応答すると接続が確立すること、制御メッセージが両方向に届くこと、
// 受信の途絶と再接続、最初の接続の失敗、返送のジッターバッファへの受け渡し、親機の送信元の
// 変化への追従を、仮想時刻で確かめる。

#include "host_connection.h"

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_network_environment.h"
#include "fakes/fake_random_source.h"
#include "fakes/fake_udp_endpoint.h"
#include "rendezvous_messages.h"
#include "return_frames.h"
#include "test_host.h"

namespace vocroz::client {
namespace {

using namespace std::chrono_literals;
namespace net = network;

const net::UdpAddress kServer = net::UdpAddress::from_ipv4({198, 51, 100, 1}, kRendezvousUdpPort);
const net::UdpAddress kHostAddress = net::UdpAddress::from_ipv4({203, 0, 113, 9}, 50000);
const net::RendezvousId kRendezvousId = {7, 7, 7};

class RecordingListener final : public HostLinkListener {
public:
    void on_link_established() override {
        ++established;
    }
    void on_link_message(std::span<const std::uint8_t> message) override {
        messages.emplace_back(message.begin(), message.end());
    }
    void on_link_lost() override {
        ++lost;
    }
    void on_link_resumed() override {
        ++resumed;
    }
    void on_link_failed(control_protocol::ErrorCode reason) override {
        failures.push_back(reason);
    }
    int established = 0;
    int lost = 0;
    int resumed = 0;
    std::vector<std::vector<std::uint8_t>> messages;
    std::vector<control_protocol::ErrorCode> failures;
};

class HostConnectionTest : public ::testing::Test {
protected:
    HostConnectionTest() {
        random_.value = 0x80000000U;  // バックオフのジッターを 0 付近にする
        environment_.resolved = {kServer};
        environment_.interfaces = {net::UdpAddress::from_ipv4({192, 168, 1, 20}, 0)};
    }

    session::HostEndpoint endpoint() const {
        session::HostEndpoint e;
        std::copy(host_.public_key().begin(), host_.public_key().end(), e.host_public_key.begin());
        std::copy(kRendezvousId.begin(), kRendezvousId.end(), e.rendezvous_id.begin());
        return e;
    }

    // 子機の送信（ソケットへの直接の送信と、送信スレッドの送信）を進め、親機宛ての
    // データグラムを親機へ渡す。
    void step(std::chrono::milliseconds dt = 10ms) {
        clock_.advance(dt);
        connection_.poll();
        sender_.pump();
        for (; delivered_ < endpoint_.sent.size(); ++delivered_) {
            const auto& sent = endpoint_.sent[delivered_];
            if (sent.to == kHostAddress) {
                host_.receive(sent.datagram);
            }
        }
    }

    // サーバーへの REGISTER の txid に合わせて、REGISTERED と PUNCH を返す。
    void answer_rendezvous() {
        std::optional<net::wire::TxId> tx_id;
        for (const auto& sent : endpoint_.sent) {
            if (sent.to == kServer) {
                tx_id = net::test::tx_id_of(sent.datagram);
            }
        }
        ASSERT_TRUE(tx_id);
        router_.on_datagram(
            kServer, net::test::registered(*tx_id, false, true,
                                           net::UdpAddress::from_ipv4({198, 18, 0, 1}, 40000)));
        router_.on_datagram(kServer, net::test::punch(*tx_id, kRendezvousId, net::PeerId{9},
                                                      net::RendezvousRole::kHost, {kHostAddress}));
    }

    void establish() {
        connection_.connect(endpoint());
        step();
        answer_rendezvous();
        for (int i = 0; i < 20 && listener_.established == 0; ++i) {
            step();
        }
        ASSERT_EQ(listener_.established, 1);
    }

    common::FakeClock clock_;
    common::FakeLogger logger_;
    transport::FakeRandomSource random_;
    net::FakeUdpEndpoint endpoint_;
    PeerInbox inbox_;
    std::unique_ptr<transport::PlayoutStream> returns_ =
        transport::PlayoutStream::create(transport::PlayoutConfig{}).value();
    HostReceivePath path_{inbox_, *returns_};
    net::ReceiveRouter router_{path_};
    transport::SendWakeup wakeup_;
    PacketSender sender_{wakeup_};
    FakeNetworkEnvironment environment_;
    transport::StaticKeyPair keys_ = test::make_keys();
    RecordingListener listener_;
    test::TestHost host_{clock_, kHostAddress, router_};
    HostConnection connection_{HostConnection::Ports{.endpoint = endpoint_,
                                                     .router = router_,
                                                     .inbox = inbox_,
                                                     .path = path_,
                                                     .sender = sender_,
                                                     .environment = environment_,
                                                     .random = random_,
                                                     .keys = keys_,
                                                     .clock = clock_,
                                                     .logger = logger_,
                                                     .listener = listener_},
                               "rendezvous.example", kRendezvousUdpPort};
    std::size_t delivered_ = 0;
};

TEST_F(HostConnectionTest, EstablishesLinkThroughRendezvousAndHandshake) {
    establish();
    EXPECT_TRUE(connection_.linked());
    EXPECT_EQ(host_.handshakes, 1);
}

TEST_F(HostConnectionTest, CarriesControlMessagesBothWays) {
    establish();
    const std::vector<std::uint8_t> join = {1, 2, 3};
    connection_.send(join);
    step();
    ASSERT_EQ(host_.messages.size(), 1U);
    EXPECT_EQ(host_.messages[0], join);

    const std::vector<std::uint8_t> approved = {4, 5};
    host_.send(approved);
    step();
    ASSERT_EQ(listener_.messages.size(), 1U);
    EXPECT_EQ(listener_.messages[0], approved);
}

TEST_F(HostConnectionTest, ReportsLossWhenHostGoesSilent) {
    establish();
    // 親機から何も届かないまま 5 秒を超える。
    for (int i = 0; i < 60; ++i) {
        clock_.advance(100ms);
        connection_.poll();
    }
    EXPECT_EQ(listener_.lost, 1);
}

TEST_F(HostConnectionTest, ReconnectsWithNewHandshakeAfterBackoff) {
    establish();
    connection_.reconnect();
    step();
    EXPECT_FALSE(connection_.linked());
    const auto registers_before = endpoint_.count_to(kServer);
    // バックオフの 1 秒（±20%）の後に、ランデブーからやり直す。
    for (int i = 0; i < 13; ++i) {
        step(100ms);
    }
    EXPECT_GT(endpoint_.count_to(kServer), registers_before);
    answer_rendezvous();
    for (int i = 0; i < 20 && listener_.established < 2; ++i) {
        step();
    }
    EXPECT_EQ(listener_.established, 2);
    EXPECT_EQ(host_.handshakes, 2);
    EXPECT_TRUE(listener_.failures.empty());
}

TEST_F(HostConnectionTest, ReportsUnreachableWhenFirstAttemptTimesOut) {
    connection_.connect(endpoint());
    // サーバーから応答がないまま、子機の打ち切り（10 秒）を過ぎる。
    for (int i = 0; i < 120; ++i) {
        step(100ms);
    }
    ASSERT_EQ(listener_.failures.size(), 1U);
    EXPECT_EQ(listener_.failures[0], control_protocol::ErrorCode::kUnreachable);
}

TEST_F(HostConnectionTest, ReportsUnreachableWhenServerNameCannotBeResolved) {
    environment_.fail_resolve = true;
    connection_.connect(endpoint());
    step();
    ASSERT_EQ(listener_.failures.size(), 1U);
    EXPECT_EQ(listener_.failures[0], control_protocol::ErrorCode::kUnreachable);
}

TEST_F(HostConnectionTest, CloseStopsSendingAndDropsLink) {
    establish();
    connection_.close();
    step();
    EXPECT_FALSE(connection_.linked());
    const auto sent_before = endpoint_.sent.size();
    connection_.send(std::vector<std::uint8_t>{1});
    step(2s);
    EXPECT_EQ(endpoint_.sent.size(), sent_before);
}

TEST_F(HostConnectionTest, PassesTheReturnToTheJitterBufferOnceTheHostUnmutes) {
    establish();
    const auto opus = test::encode_return_frame();
    // 親機が返送を解除する前のフレーム（世代 0）は鳴らさない。
    host_.send_media(0, 0, opus);
    connection_.apply_return_mute(false, 1);
    for (std::uint64_t i = 0; i < 8; ++i) {
        host_.send_media(i * test::kReturnFrameSamples, 1, opus);
    }
    std::vector<float> out(2400);
    returns_->render(out);
    EXPECT_GT(returns_->stats().frames_decoded, 0U);
}

TEST_F(HostConnectionTest, KeepsTheLinkAliveWithReturnMedia) {
    establish();
    const auto opus = test::encode_return_frame();
    connection_.apply_return_mute(false, 1);
    // 制御パケットが届かなくても、返送のメディアが届いていれば途絶とみなさない。
    for (std::uint64_t i = 0; i < 60; ++i) {
        host_.send_media(i * test::kReturnFrameSamples, 1, opus);
        clock_.advance(100ms);
        connection_.poll();
    }
    EXPECT_EQ(listener_.lost, 0);
}

TEST_F(HostConnectionTest, FollowsTheHostToANewAddress) {
    establish();
    const auto moved = net::UdpAddress::from_ipv4({203, 0, 113, 9}, 50001);
    host_.from = moved;
    host_.send(std::vector<std::uint8_t>{1});
    step();
    connection_.send(std::vector<std::uint8_t>{2});
    step();
    EXPECT_EQ(endpoint_.sent.back().to, moved);
}

}  // namespace
}  // namespace vocroz::client
