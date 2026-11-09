// SPDX-License-Identifier: AGPL-3.0-only
//
// 悪条件の回線での、子機から親機へのメディアの結合テスト（AT-A06、AT-A17 の CI
// で確かめられる部分）。
// 実際の伝送の部品（Opus、暗号化とリプレイ排除、信号状態、ジッターバッファ）を、
// シード固定の障害を注入したシミュレーションのネットワーク越しにつなぐ。
// - 順序の入れ替え、重複、欠損、ジッターでも破綻せず音が出続ける
// - 500ms 以上の途絶で NO_SIGNAL になって無音になり、詰まっていた古い音を遅れて鳴らさない
// - 改ざんしたパケットは復号の段階ですべて捨て、音に影響しない

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/test_harness/media_link_simulation.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/signal_state.h"

namespace vocroz::test_harness {
namespace {

using std::chrono::milliseconds;
using transport::SignalState;

constexpr std::uint64_t kSamplesPerMs = transport::kSampleRate / 1000;

constexpr std::uint64_t ms(std::uint64_t value) {
    return value * kSamplesPerMs;
}

// 出力を時刻つきで記録し、区間の RMS を返す。
class OutputRecorder {
public:
    void record(std::uint64_t first, std::span<const float> block) {
        if (samples_.size() < first + block.size()) {
            samples_.resize(first + block.size());
        }
        std::copy(block.begin(), block.end(),
                  samples_.begin() + static_cast<std::ptrdiff_t>(first));
    }

    // [begin, end) の RMS。
    [[nodiscard]] double rms(std::uint64_t begin, std::uint64_t end) const {
        double sum = 0.0;
        for (std::uint64_t i = begin; i < end && i < samples_.size(); ++i) {
            sum += static_cast<double>(samples_[i]) * samples_[i];
        }
        return end > begin ? std::sqrt(sum / static_cast<double>(end - begin)) : 0.0;
    }

private:
    std::vector<float> samples_;
};

// tone_until より前は 1kHz の正弦波（RMS 約 0.35）、それ以降は無音を送る。
MediaLinkSimulation::Source tone_until(std::uint64_t tone_until) {
    return [tone_until](std::uint64_t first, std::span<float> block) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            const std::uint64_t position = first + i;
            const double t = static_cast<double>(position) / transport::kSampleRate;
            block[i] = position < tone_until
                           ? static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * t))
                           : 0.0F;
        }
    };
}

class MediaFaultsTest : public ::testing::Test {
protected:
    void create(const netsim::FaultProfile& uplink) {
        MediaLinkConfig config;
        config.faults = uplink;
        auto created = MediaLinkSimulation::create(config);
        ASSERT_TRUE(created);
        simulation_ = std::move(created).value();
    }

    void run_until(std::uint64_t until, const MediaLinkSimulation::Source& source) {
        simulation_->run(until - simulation_->now_samples(), source,
                         [this](std::uint64_t first, std::span<const float> block) {
                             output_.record(first, block);
                         });
    }

    void begin_outage(netsim::OutageMode mode) {
        simulation_->network().begin_outage(MediaLinkSimulation::kClientAddress,
                                            MediaLinkSimulation::kHostAddress, mode);
    }

    void end_outage() {
        simulation_->network().end_outage(MediaLinkSimulation::kClientAddress,
                                          MediaLinkSimulation::kHostAddress);
    }

    [[nodiscard]] netsim::LinkStats uplink_stats() const {
        return simulation_->network().stats(MediaLinkSimulation::kClientAddress,
                                            MediaLinkSimulation::kHostAddress);
    }

    std::unique_ptr<MediaLinkSimulation> simulation_;
    OutputRecorder output_;
};

TEST_F(MediaFaultsTest, KeepsPlayingThroughReorderDuplicateLossAndJitter) {
    netsim::FaultProfile faults;
    faults.delay = milliseconds{5};
    faults.jitter = milliseconds{3};
    faults.loss = 0.05;
    faults.duplicate = 0.05;
    faults.reorder = 0.05;
    faults.reorder_delay = milliseconds{4};
    faults.seed = 2026;
    create(faults);
    const auto source = tone_until(ms(60'000));
    run_until(ms(5'000), source);

    const auto link = uplink_stats();
    EXPECT_GT(link.lost, 0U);
    EXPECT_GT(link.duplicated, 0U);
    EXPECT_GT(link.reordered, 0U);
    const auto& receiver = simulation_->receiver();
    // 重複はジッターバッファに届く前に、リプレイ排除で捨てる。
    // 重複の 2 つ目は遅れて届くので、終了の時点でまだ経路上にあるものは数に入らない。
    EXPECT_GT(receiver.stats().rejected_replay, 0U);
    EXPECT_LE(receiver.stats().rejected_replay, link.duplicated);
    EXPECT_EQ(receiver.stats().rejected_invalid, 0U);
    EXPECT_GT(receiver.playout_stats().frames_concealed, 0U);
    EXPECT_EQ(receiver.signal_state(), SignalState::kReceiving);
    // 最後の 1 秒も音が出ている（欠損は PLC で埋まり、途切れ続けない）。
    EXPECT_GT(output_.rms(ms(4'000), ms(5'000)), 0.25);
}

TEST_F(MediaFaultsTest, GoesSilentAfter500msOutageAndRecovers) {
    netsim::FaultProfile faults;
    faults.delay = milliseconds{5};
    create(faults);
    const auto source = tone_until(ms(60'000));
    run_until(ms(1'000), source);
    ASSERT_EQ(simulation_->receiver().signal_state(), SignalState::kReceiving);

    begin_outage(netsim::OutageMode::kDrop);
    run_until(ms(1'400), source);
    EXPECT_EQ(simulation_->receiver().signal_state(), SignalState::kReceiving);
    run_until(ms(1'600), source);
    EXPECT_EQ(simulation_->receiver().signal_state(), SignalState::kNoSignal);
    run_until(ms(2'000), source);
    // 途絶の間、古いバッファを繰り返し鳴らさず無音にする。
    EXPECT_LT(output_.rms(ms(1'550), ms(2'000)), 1e-3);

    end_outage();
    run_until(ms(3'000), source);
    EXPECT_EQ(simulation_->receiver().signal_state(), SignalState::kReceiving);
    EXPECT_GT(output_.rms(ms(2'500), ms(3'000)), 0.25);
}

TEST_F(MediaFaultsTest, DoesNotPlayStaleAudioReleasedAfterStall) {
    // 経路上のバッファが 1 秒詰まり、その間の音（正弦波）がまとめて届く。
    // 送信側は詰まりが解けた時点から無音を送るので、遅れて届いた正弦波が鳴り続けたら古い音を再生している。
    netsim::FaultProfile faults;
    faults.delay = milliseconds{5};
    create(faults);
    constexpr std::uint64_t kRelease = ms(2'500);
    const auto source = tone_until(kRelease);
    run_until(ms(1'500), source);
    begin_outage(netsim::OutageMode::kHold);
    run_until(kRelease, source);
    EXPECT_EQ(simulation_->receiver().signal_state(), SignalState::kNoSignal);
    end_outage();
    run_until(ms(4'000), source);

    EXPECT_GT(uplink_stats().held_in_outage, 100U);
    // まとめて届いた古いフレームは、深さの上限を超えた分を捨てて目標まで浅くするので、
    // 鳴っても上限（20ms）とフレーム長程度。それより後は無音になる。
    EXPECT_LT(output_.rms(kRelease + ms(60), ms(4'000)), 1e-3);
    EXPECT_GT(simulation_->receiver().playout_stats().frames_dropped_overflow +
                  simulation_->receiver().playout_stats().frames_late,
              100U);
}

TEST_F(MediaFaultsTest, DropsTamperedPacketsAtDecryption) {
    netsim::FaultProfile faults;
    faults.delay = milliseconds{5};
    faults.corrupt = 0.1;
    faults.duplicate = 0.05;
    faults.seed = 17;
    create(faults);
    const auto source = tone_until(ms(60'000));
    run_until(ms(3'000), source);

    const auto link = uplink_stats();
    const auto& stats = simulation_->receiver().stats();
    ASSERT_GT(link.corrupted, 0U);
    // 1 ビットでも書き換えたパケットは、ヘッダーでも暗号文でも復号の段階で捨てる。
    EXPECT_EQ(stats.rejected_invalid, link.corrupted);
    // 書き換えていない重複はリプレイとして捨てる。元のパケットが改ざんで捨てられていた場合は、
    // 重複の方が初めて届いた正規のパケットになるので、リプレイの数は重複の数以下になる。
    EXPECT_LE(stats.rejected_replay, link.duplicated);
    EXPECT_EQ(stats.media_frames + stats.rejected_invalid + stats.rejected_replay, stats.datagrams);
    EXPECT_GT(output_.rms(ms(2'000), ms(3'000)), 0.25);
}

}  // namespace
}  // namespace vocroz::test_harness
