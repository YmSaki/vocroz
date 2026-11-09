// SPDX-License-Identifier: AGPL-3.0-only
//
// DeviceLossRelay の単体テスト。

#include "device_loss_relay.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using std::chrono_literals::operator""ms;

// 安全ミュートは通知を受けたスレッドですぐにかけ、後始末はループのスレッドで行う。
TEST(DeviceLossRelayTest, MutesImmediatelyAndCleansUpOnTheLoop) {
    audio_core::Mixer mixer;
    ControlLoop loop;
    std::thread::id cleanup_thread;
    DeviceLossRelay relay(mixer, loop, [&] {
        cleanup_thread = std::this_thread::get_id();
        loop.request_stop();
    });
    std::thread device_thread([&] {
        relay.on_device_lost();
        EXPECT_TRUE(mixer.is_safety_muted());
    });
    device_thread.join();
    loop.run(1000ms, [] {});
    EXPECT_EQ(cleanup_thread, std::this_thread::get_id());
}

}  // namespace
}  // namespace vocroz::engine
