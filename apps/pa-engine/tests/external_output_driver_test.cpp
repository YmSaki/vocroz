// SPDX-License-Identifier: AGPL-3.0-only
//
// ExternalOutputDriver の単体テスト（docs/adr/0040-srtp-external-output.md）。本物の Mixer と
// EngineMixer を使い、送り出すバスの音がストリームタップから送信スレッドを通って SRTP の
// パケットとして宛先のソケットへ出ること、止めたら送らないこと、ソケットを開けなければ何も
// 送らないこと、接続テストが試験音を送って結果を返すことを確かめる。
// パケットの暗号化と RTP の形式そのものは transport の単体テストで libsrtp の受信側を使って
// 確かめているので、ここでは平文の RTP ヘッダーの部分（SRTP でも暗号化されない）だけを見る。

#include "external_output_driver.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"
#include "vocroz/transport/srtp_master_key.h"

#include "engine_constants.h"
#include "fakes/fake_audio_source.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_random_source.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;
using namespace std::chrono_literals;

constexpr const char* kKey = "inline:AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0e";
// RTP の固定ヘッダーの長さ。
constexpr std::size_t kRtpHeaderBytes = 12;
// 送信スレッドを待つ上限。CI の遅いマシンでも越えない長さにする。
constexpr auto kWaitLimit = 5s;

// 送信スレッドから送られたデータグラムを記録するソケット。テストのスレッドから待てるようにする。
class RecordingSocket final : public transport::DatagramSocket {
public:
    struct Shared {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::vector<std::uint8_t>> sent;
        bool fail_sends = false;
    };

    explicit RecordingSocket(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}

    common::Result<void, transport::TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept override {
        const std::scoped_lock lock(shared_->mutex);
        if (shared_->fail_sends) {
            return common::make_unexpected(transport::TransportError::kSendFailed);
        }
        shared_->sent.emplace_back(datagram.begin(), datagram.end());
        shared_->changed.notify_all();
        return {};
    }

private:
    std::shared_ptr<Shared> shared_;
};

class FakeSocketOpener final : public ExternalSocketOpener {
public:
    common::Result<std::unique_ptr<transport::DatagramSocket>, cp::ExternalOutputResult> open(
        std::string_view host, std::uint16_t port) override {
        opened.emplace_back(std::string(host), port);
        if (error) {
            return common::make_unexpected(*error);
        }
        return std::unique_ptr<transport::DatagramSocket>(
            std::make_unique<RecordingSocket>(shared));
    }

    std::size_t sent_count() {
        const std::scoped_lock lock(shared->mutex);
        return shared->sent.size();
    }

    std::vector<std::vector<std::uint8_t>> sent() {
        const std::scoped_lock lock(shared->mutex);
        return shared->sent;
    }

    bool wait_for_packets(std::size_t count) {
        std::unique_lock lock(shared->mutex);
        return shared->changed.wait_for(lock, kWaitLimit,
                                        [&] { return shared->sent.size() >= count; });
    }

    std::shared_ptr<RecordingSocket::Shared> shared = std::make_shared<RecordingSocket::Shared>();
    std::vector<std::pair<std::string, std::uint16_t>> opened;
    std::optional<cp::ExternalOutputResult> error;
};

class ExternalOutputDriverTest : public ::testing::Test {
protected:
    ExternalOutputDriverTest()
        : mixer_(ac::MixerConfig{kMixerFaderCapacity, kMixerMeterCapacity}),
          engine_(mixer_, logger_),
          driver_(
              engine_, sockets_, random_,
              [this](cp::ExternalOutputResult result) { test_result_.set_value(result); }, 50ms) {}

    static session::ExternalOutputPlan plan() {
        session::ExternalOutputPlan p;
        p.host = "192.0.2.10";
        p.port = 5004;
        p.source_id = "master";
        p.channels = 2;
        p.payload_type = 100;
        return p;
    }

    static transport::SrtpMasterKey key() {
        return transport::parse_srtp_master_key(kKey).value();
    }

    // ストリップに音を流し、ストリップと Master のミュートを解く。
    void make_master_audible() {
        engine_.add_channel("ch-1");
        ASSERT_TRUE(engine_.attach_source(
            "ch-1", std::make_shared<ac::FakeAudioSource>(ac::FakeAudioSource::constant(0.2F))));
        engine_.set_channel_muted("ch-1", false);
        engine_.set_master_muted(false);
    }

    // 1 回のコールバックぶん（1024 フレーム、5ms のフレームで 4 つ強）を描画する。
    void render() {
        std::vector<float> out(1024 * 2, 0.0F);
        mixer_.render(ac::InterleavedBuffer{out, 2});
        engine_.collect();
    }

    common::FakeLogger logger_;
    ac::Mixer mixer_;
    EngineMixer engine_;
    FakeSocketOpener sockets_;
    transport::FakeRandomSource random_;
    std::promise<cp::ExternalOutputResult> test_result_;
    ExternalOutputDriver driver_;
};

TEST_F(ExternalOutputDriverTest, SendsRtpPacketsOfTheSourceBusUntilStopped) {
    make_master_audible();
    ASSERT_TRUE(driver_.start(plan(), key()));
    ASSERT_EQ(sockets_.opened.size(), 1U);
    EXPECT_EQ(sockets_.opened[0], (std::pair<std::string, std::uint16_t>{"192.0.2.10", 5004}));

    render();
    ASSERT_TRUE(sockets_.wait_for_packets(4));
    const auto sent = sockets_.sent();
    for (const auto& packet : sent) {
        ASSERT_GT(packet.size(), kRtpHeaderBytes);
        // RTP のバージョン 2、パディングなし、ヘッダー拡張なし、CSRC なし。
        EXPECT_EQ(packet[0], 0x80);
        EXPECT_EQ(packet[1] & 0x7F, 100);
    }
    // 最初のパケットにだけマーカーを立てる。
    EXPECT_NE(sent[0][1] & 0x80, 0);
    EXPECT_EQ(sent[1][1] & 0x80, 0);

    driver_.stop();
    const auto after_stop = sockets_.sent_count();
    render();
    render();
    EXPECT_EQ(sockets_.sent_count(), after_stop);
}

TEST_F(ExternalOutputDriverTest, ResolveFailureSendsNothing) {
    make_master_audible();
    sockets_.error = cp::ExternalOutputResult::kResolveFailed;
    const auto started = driver_.start(plan(), key());
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error(), cp::ExternalOutputResult::kResolveFailed);
    render();
    EXPECT_EQ(sockets_.sent_count(), 0U);
}

TEST_F(ExternalOutputDriverTest, UnknownSourceBusIsNotTapped) {
    make_master_audible();
    auto missing = plan();
    missing.source_id = "bus-missing";
    ASSERT_TRUE(driver_.start(missing, key()));
    render();
    render();
    EXPECT_EQ(sockets_.sent_count(), 0U);
}

TEST_F(ExternalOutputDriverTest, ConnectionTestSendsTheToneAndReportsOk) {
    auto future = test_result_.get_future();
    ASSERT_TRUE(driver_.start_test(plan(), key()));
    ASSERT_EQ(future.wait_for(kWaitLimit), std::future_status::ready);
    EXPECT_EQ(future.get(), cp::ExternalOutputResult::kOk);
    // 50ms の試験音は 5ms のフレームで 10 個。
    EXPECT_EQ(sockets_.sent_count(), 10U);
}

TEST_F(ExternalOutputDriverTest, ConnectionTestReportsSendFailure) {
    sockets_.shared->fail_sends = true;
    auto future = test_result_.get_future();
    ASSERT_TRUE(driver_.start_test(plan(), key()));
    ASSERT_EQ(future.wait_for(kWaitLimit), std::future_status::ready);
    EXPECT_EQ(future.get(), cp::ExternalOutputResult::kSendFailed);
}

}  // namespace
}  // namespace vocroz::engine
