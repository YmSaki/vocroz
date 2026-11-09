// SPDX-License-Identifier: AGPL-3.0-only
//
// SendWakeup の単体テスト。合図が世代として数えられ、待つ前に届いた合図も取りこぼさないこと、
// 別のスレッドからの合図で待ちが解けることを確かめる。

#include "vocroz/transport/send_wakeup.h"

#include <thread>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

TEST(SendWakeupTest, NotifyAdvancesTheGeneration) {
    SendWakeup wakeup;
    const auto before = wakeup.generation();
    wakeup.notify();
    wakeup.notify();
    EXPECT_EQ(wakeup.generation(), before + 2);
}

TEST(SendWakeupTest, WaitReturnsAtOnceWhenNotifiedAfterReading) {
    // 処理の途中で届いた合図は、次の wait() をすぐに戻す（取りこぼさない）。
    SendWakeup wakeup;
    const auto seen = wakeup.generation();
    wakeup.notify();
    wakeup.wait(seen);
    SUCCEED();
}

TEST(SendWakeupTest, NotifyFromAnotherThreadWakesTheWaiter) {
    SendWakeup wakeup;
    const auto seen = wakeup.generation();
    std::thread notifier([&wakeup] { wakeup.notify(); });
    wakeup.wait(seen);
    notifier.join();
    EXPECT_NE(wakeup.generation(), seen);
}

}  // namespace
}  // namespace vocroz::transport
