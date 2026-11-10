// SPDX-License-Identifier: AGPL-3.0-only
//
// ThreadPeerSendScheduler の単体テスト。送信待ちに入れたものが送信スレッドで送られること、
// stop() で残りを送り切ってから止まること、止めた後の合図と 2 回目の stop() が何もしないことを
// 確かめる。

#include "thread_peer_send_scheduler.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/static_key_pair.h"

#include "fakes/fake_datagram_socket.h"

namespace vocroz::engine {
namespace {

namespace tp = transport;
using Bytes = std::vector<std::uint8_t>;

// Noise_IK のハンドシェイクを済ませ、親機側の送信方向の暗号化を返す。
tp::PacketSealer host_sealer() {
    auto client_keys = tp::StaticKeyPair::generate();
    auto host_keys = tp::StaticKeyPair::generate();
    EXPECT_TRUE(client_keys && host_keys);
    auto initiator = tp::NoiseIkHandshake::create_initiator(
        client_keys.value(), host_keys.value().public_key(), 0x01020304);
    auto responder = tp::NoiseIkHandshake::create_responder(host_keys.value());
    EXPECT_TRUE(initiator && responder);
    Bytes packet(tp::kMaxPacketBytes);
    Bytes payload(tp::kMaxPacketBytes);
    const auto initiation = initiator.value().write_initiation({}, packet);
    EXPECT_TRUE(
        responder.value().read_initiation(std::span(packet).first(initiation.value()), payload));
    static_cast<void>(responder.value().write_response({}, packet));
    auto host = responder.value().finish();
    EXPECT_TRUE(host);
    return std::move(host).value().sealer;
}

TEST(ThreadPeerSendSchedulerTest, SendsQueuedPacketsOnItsThread) {
    ThreadPeerSendScheduler scheduler;
    auto worker = scheduler.create();
    tp::FakeDatagramSocket socket;
    tp::PeerPacketSender sender(host_sealer(), socket, tp::kReturnStreamId, *worker);
    worker->start(sender);

    const Bytes plaintext{1, 2, 3};
    ASSERT_TRUE(sender.send_control_packet(plaintext));
    // 合図で起きた送信スレッドが送るのを待つ。止まっていれば期限で失敗する。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (sender.stats().control_sent == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(sender.stats().control_sent, 1U);
    worker->stop();
    EXPECT_EQ(socket.datagrams.size(), 1U);
}

TEST(ThreadPeerSendSchedulerTest, StopSendsWhatIsLeftBeforeStopping) {
    ThreadPeerSendScheduler scheduler;
    auto worker = scheduler.create();
    tp::FakeDatagramSocket socket;
    tp::PeerPacketSender sender(host_sealer(), socket, tp::kReturnStreamId, *worker);
    worker->start(sender);

    const Bytes plaintext{4, 5, 6};
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(sender.send_control_packet(plaintext));
    }
    worker->stop();
    EXPECT_EQ(socket.datagrams.size(), 3U);
}

TEST(ThreadPeerSendSchedulerTest, NotifyAndSecondStopAfterStopDoNothing) {
    ThreadPeerSendScheduler scheduler;
    auto worker = scheduler.create();
    tp::FakeDatagramSocket socket;
    tp::PeerPacketSender sender(host_sealer(), socket, tp::kReturnStreamId, *worker);
    worker->start(sender);
    worker->stop();

    // 止めた後に入れたものは送らない（送信に使うソケットはもう閉じているかもしれない）。
    const Bytes plaintext{7};
    ASSERT_TRUE(sender.send_control_packet(plaintext));
    worker->stop();
    EXPECT_TRUE(socket.datagrams.empty());
}

TEST(ThreadPeerSendSchedulerTest, WorkerThatNeverStartedCanBeDestroyed) {
    ThreadPeerSendScheduler scheduler;
    auto worker = scheduler.create();
    worker->notify();
    worker->stop();
    worker.reset();
    SUCCEED();
}

}  // namespace
}  // namespace vocroz::engine
