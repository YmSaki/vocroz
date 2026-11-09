// SPDX-License-Identifier: AGPL-3.0-only
//
// EngineAttacher のテスト。動いているエンジンへの再アタッチ、起動してからの接続、終了コード 3 の
// 扱い、起動は 1 回の手順で 1 回だけであること、失敗の知らせ方を、仮想時刻で確かめる。

#include "engine_attacher.h"

#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "app_constants.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_message_channel.h"

namespace vocroz::pa_desktop {
namespace {

/// エンジンの起動の何回目からつながるかを決められるフェイク。
class FakeEngine final : public EngineConnector, public EngineLauncher {
public:
    common::Result<std::unique_ptr<control_protocol::MessageChannel>, ConnectError> connect()
        override {
        ++connect_calls;
        if (refuse) {
            return common::make_unexpected(ConnectError::kRefused);
        }
        if (!running) {
            return common::make_unexpected(ConnectError::kNotRunning);
        }
        return std::unique_ptr<control_protocol::MessageChannel>(
            std::make_unique<control_protocol::FakeMessageChannel>());
    }
    bool launch() override {
        ++launches;
        exited.reset();
        return can_launch;
    }
    std::optional<int> exit_code() override {
        return exited;
    }

    bool running = false;
    bool refuse = false;
    bool can_launch = true;
    std::optional<int> exited;
    int connect_calls = 0;
    int launches = 0;
};

class RecordingListener final : public EngineAttachListener {
public:
    void on_engine_attached(std::unique_ptr<control_protocol::MessageChannel> channel) override {
        channels.push_back(std::move(channel));
    }
    void on_engine_attach_failed(AttachFailure failure) override {
        failures.push_back(failure);
    }
    std::vector<std::unique_ptr<control_protocol::MessageChannel>> channels;
    std::vector<AttachFailure> failures;
};

class EngineAttacherTest : public ::testing::Test {
protected:
    // 手順を、つながるか失敗するまで（上限つきで）進める。
    void run_until_done() {
        for (int i = 0; i < 1000 && attacher.attaching(); ++i) {
            attacher.tick();
            clock.advance(kAttachTickInterval);
        }
    }

    FakeEngine engine;
    common::FakeClock clock;
    RecordingListener listener;
    EngineAttacher attacher{engine, engine, clock, listener};
};

TEST_F(EngineAttacherTest, ReattachesToRunningEngineWithoutLaunching) {
    engine.running = true;
    attacher.begin();
    attacher.tick();
    EXPECT_EQ(listener.channels.size(), 1U);
    EXPECT_EQ(engine.launches, 0);
    EXPECT_FALSE(attacher.attaching());
}

TEST_F(EngineAttacherTest, LaunchesOnceAndConnectsWhenThePipeAppears) {
    attacher.begin();
    attacher.tick();
    EXPECT_EQ(engine.launches, 1);
    EXPECT_TRUE(attacher.attaching());

    // 起動の途中は試し直すが、起動はし直さない。
    for (int i = 0; i < 10; ++i) {
        clock.advance(kEngineConnectRetryInterval);
        attacher.tick();
    }
    EXPECT_EQ(engine.launches, 1);
    EXPECT_TRUE(listener.channels.empty());

    engine.running = true;
    clock.advance(kEngineConnectRetryInterval);
    attacher.tick();
    EXPECT_EQ(listener.channels.size(), 1U);
    EXPECT_TRUE(listener.failures.empty());
}

TEST_F(EngineAttacherTest, KeepsConnectingWhenTheLaunchedEngineSaysAlreadyRunning) {
    attacher.begin();
    attacher.tick();
    engine.exited = kEngineExitAlreadyRunning;
    clock.advance(kEngineConnectRetryInterval);
    attacher.tick();
    EXPECT_TRUE(attacher.attaching());

    engine.running = true;
    run_until_done();
    EXPECT_EQ(listener.channels.size(), 1U);
    EXPECT_EQ(engine.launches, 1);
}

TEST_F(EngineAttacherTest, FailsWhenTheLaunchedEngineExitsWithAnError) {
    attacher.begin();
    attacher.tick();
    engine.exited = 1;
    run_until_done();
    ASSERT_EQ(listener.failures.size(), 1U);
    EXPECT_EQ(listener.failures[0], AttachFailure::kEngineExited);
}

TEST_F(EngineAttacherTest, FailsWhenTheEngineCannotBeLaunched) {
    engine.can_launch = false;
    attacher.begin();
    attacher.tick();
    ASSERT_EQ(listener.failures.size(), 1U);
    EXPECT_EQ(listener.failures[0], AttachFailure::kLaunchFailed);
}

TEST_F(EngineAttacherTest, TimesOutWhenThePipeNeverAppears) {
    attacher.begin();
    attacher.tick();
    clock.advance(kEngineStartTimeout - kAttachTickInterval);
    attacher.tick();
    EXPECT_TRUE(attacher.attaching());
    run_until_done();
    ASSERT_EQ(listener.failures.size(), 1U);
    EXPECT_EQ(listener.failures[0], AttachFailure::kTimedOut);
    EXPECT_EQ(engine.launches, 1);
}

TEST_F(EngineAttacherTest, RefusedPipeFailsWithoutLaunching) {
    engine.refuse = true;
    attacher.begin();
    attacher.tick();
    ASSERT_EQ(listener.failures.size(), 1U);
    EXPECT_EQ(listener.failures[0], AttachFailure::kRefused);
    EXPECT_EQ(engine.launches, 0);
}

TEST_F(EngineAttacherTest, RetryAfterFailureMayLaunchAgain) {
    // 自動では起動し直さず、利用者が再試行したときだけ、もう一度起動する。
    engine.exited = std::nullopt;
    attacher.begin();
    attacher.tick();
    engine.exited = 1;
    run_until_done();
    ASSERT_EQ(listener.failures.size(), 1U);
    attacher.tick();
    EXPECT_EQ(engine.launches, 1);

    attacher.begin();
    attacher.tick();
    EXPECT_EQ(engine.launches, 2);
}

TEST_F(EngineAttacherTest, WaitsForTheRetryIntervalBetweenAttempts) {
    attacher.begin();
    attacher.tick();
    const int calls = engine.connect_calls;
    clock.advance(kEngineConnectRetryInterval / 2);
    attacher.tick();
    EXPECT_EQ(engine.connect_calls, calls);
    clock.advance(kEngineConnectRetryInterval / 2);
    attacher.tick();
    EXPECT_EQ(engine.connect_calls, calls + 1);
}

}  // namespace
}  // namespace vocroz::pa_desktop
