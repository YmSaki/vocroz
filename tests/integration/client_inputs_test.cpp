// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の入力拡張の結合テスト（docs/adr/0035-client-input-expansion.md）。
// 1 台の子機がステレオのマイクとアプリ音声を送り、親機で入力ごとの別のストリップに、左右を
// 分けたまま届くことを、session、transport、試験基盤のネットワークを通して確かめる。
// 入力の構成を参加中に変えたときに、変わった入力のストリップだけがミュートで作り直されることも
// 確かめる。

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/test_harness/ensemble_simulation.h"
#include "vocroz/transport/constants.h"

namespace vocroz::test_harness {
namespace {

namespace cp = control_protocol;
using namespace std::chrono_literals;

constexpr std::uint64_t kSamplesPerMs = transport::kSampleRate / 1000;

constexpr std::uint64_t ms(std::uint64_t value) {
    return value * kSamplesPerMs;
}

// マイクの左とアプリ音声の周波数。100 ms の窓にちょうど整数周期が入り、互いに重ならない。
constexpr double kMicLeftHz = 440.0;
constexpr double kAppHz = 660.0;
constexpr double kAmplitude = 0.2;
// Opus を通すと振幅は少し変わるので、送った振幅の半分を「鳴っている」の下限にする。
constexpr double kPresentAmplitude = kAmplitude / 2;
constexpr double kAbsentAmplitude = 0.01;
constexpr std::uint64_t kWindow = ms(100);

double tone(double hz, std::uint64_t sample) {
    const double t = static_cast<double>(sample) / static_cast<double>(transport::kSampleRate);
    return kAmplitude * std::sin(2.0 * std::numbers::pi * hz * t);
}

// 1 チャンネルの信号を時刻つきで記録し、区間の周波数ごとの振幅を返す。
class Track {
public:
    void record(std::uint64_t first, std::span<const float> block) {
        if (samples_.size() < first + block.size()) {
            samples_.resize(first + block.size());
        }
        std::ranges::copy(block, samples_.begin() + static_cast<std::ptrdiff_t>(first));
    }

    // [begin, end) に含まれる hz の正弦波の振幅（単一周波数の DFT）。
    [[nodiscard]] double amplitude(std::uint64_t begin, std::uint64_t end, double hz) const {
        std::complex<double> sum{};
        for (std::uint64_t i = begin; i < end && i < samples_.size(); ++i) {
            const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) /
                                 static_cast<double>(transport::kSampleRate);
            sum += static_cast<double>(samples_[i]) * std::polar(1.0, -phase);
        }
        return end > begin ? 2.0 * std::abs(sum) / static_cast<double>(end - begin) : 0.0;
    }

private:
    std::vector<float> samples_;
};

class ClientInputsTest : public ::testing::Test {
protected:
    ClientInputsTest() {
        EnsembleConfig config;
        config.client_names = {"Key"};
        config.link = netsim::parse_fault_profile("delay=10ms,jitter=2ms,seed=11").value();
        simulation_ = std::make_unique<EnsembleSimulation>(std::move(config));
    }

    cp::MixerSnapshot host_snapshot() {
        return simulation_->host().session().snapshot();
    }

    // マイクは左だけに音を入れる（ステレオなら右は無音、モノラルなら左のまま）。
    // アプリ音声は左右に同じ音を入れる。親機のストリップごとの出力を記録する。
    void play(std::chrono::milliseconds duration) {
        simulation_->run(
            ms(static_cast<std::uint64_t>(duration.count())),
            [this](std::size_t, std::uint64_t first, std::span<float> block) {
                const std::size_t channels =
                    simulation_->client(0).session().snapshot().input_channels;
                for (std::size_t i = 0; i < block.size() / channels; ++i) {
                    block[i * channels] = static_cast<float>(tone(kMicLeftHz, first + i));
                }
            },
            [this](std::uint64_t first, std::span<const float>) {
                for (const auto& channel : host_snapshot().channels) {
                    if (const auto* out = simulation_->host().strip_output(channel.entity_id)) {
                        left_[channel.entity_id].record(first, out->left);
                        right_[channel.entity_id].record(first, out->right);
                    }
                }
            },
            [](std::size_t, std::uint64_t first, std::span<float> block) {
                for (std::size_t i = 0; i < block.size() / 2; ++i) {
                    const auto value = static_cast<float>(tone(kAppHz, first + i));
                    block[i * 2] = value;
                    block[(i * 2) + 1] = value;
                }
            });
    }

    void start_session() {
        ASSERT_TRUE(
            simulation_->host_command(host_snapshot().session.entity_id, cp::CreateSession{}));
        ASSERT_TRUE(simulation_->invite());
    }

    // 子機が参加を申請し、承認されて送信を始められるところまで進める。
    void join_and_approve() {
        ASSERT_TRUE(simulation_->client_command(0, cp::Join{*simulation_->invite()}));
        play(300ms);
        const auto pending = host_snapshot().pending;
        ASSERT_EQ(pending.size(), 1U);
        ASSERT_TRUE(simulation_->host_command(pending[0].entity_id, cp::ApproveParticipant{}));
        play(300ms);
        ASSERT_EQ(simulation_->client(0).session().snapshot().client_state,
                  cp::ClientState::kConnected);
        ASSERT_TRUE(simulation_->client_command(0, cp::SetSendMute{false}));
    }

    std::optional<cp::ChannelStrip> strip(cp::InputSourceKind kind) {
        for (const auto& channel : host_snapshot().channels) {
            if (channel.source_kind == kind) {
                return channel;
            }
        }
        return std::nullopt;
    }

    void unmute_strips() {
        for (const auto& channel : host_snapshot().channels) {
            ASSERT_TRUE(simulation_->host_command(channel.entity_id, cp::SetChannelMute{false}));
        }
    }

    // [begin, end) のすべての窓で、track に hz の音が鳴っていれば true。
    static bool present(const Track& track, std::uint64_t begin, std::uint64_t end, double hz) {
        for (std::uint64_t w = begin; w + kWindow <= end; w += kWindow) {
            if (track.amplitude(w, w + kWindow, hz) < kPresentAmplitude) {
                return false;
            }
        }
        return true;
    }

    static bool absent(const Track& track, std::uint64_t begin, std::uint64_t end, double hz) {
        for (std::uint64_t w = begin; w + kWindow <= end; w += kWindow) {
            if (track.amplitude(w, w + kWindow, hz) > kAbsentAmplitude) {
                return false;
            }
        }
        return true;
    }

    std::unique_ptr<EnsembleSimulation> simulation_;
    std::map<std::string, Track> left_;
    std::map<std::string, Track> right_;
};

TEST_F(ClientInputsTest, StereoMicrophoneAndAppAudioArriveOnSeparateStrips) {
    start_session();
    ASSERT_TRUE(simulation_->client_command(0, cp::SetInputChannels{2}));
    ASSERT_TRUE(simulation_->client_command(0, cp::SetAppAudio{true}));
    join_and_approve();

    // 承認の時点で、申告した入力ごとにミュートのストリップができる。
    const auto mic = strip(cp::InputSourceKind::kMicrophone);
    const auto app = strip(cp::InputSourceKind::kApplicationAudio);
    ASSERT_TRUE(mic);
    ASSERT_TRUE(app);
    EXPECT_EQ(host_snapshot().channels.size(), 2U);
    EXPECT_EQ(mic->channel_count, 2);
    EXPECT_EQ(app->channel_count, 2);
    EXPECT_EQ(mic->participant_id, app->participant_id);
    EXPECT_TRUE(mic->channel_muted);
    EXPECT_TRUE(app->channel_muted);

    unmute_strips();
    play(500ms);
    const std::uint64_t begin = simulation_->now_samples();
    play(1000ms);
    const std::uint64_t end = simulation_->now_samples();

    // マイクの左右は分かれたまま届き、アプリ音声は別のストリップに届く。
    EXPECT_TRUE(present(left_[mic->entity_id], begin, end, kMicLeftHz));
    EXPECT_TRUE(absent(right_[mic->entity_id], begin, end, kMicLeftHz));
    EXPECT_TRUE(absent(left_[mic->entity_id], begin, end, kAppHz));
    EXPECT_TRUE(present(left_[app->entity_id], begin, end, kAppHz));
    EXPECT_TRUE(present(right_[app->entity_id], begin, end, kAppHz));
    EXPECT_TRUE(absent(left_[app->entity_id], begin, end, kMicLeftHz));
    EXPECT_EQ(simulation_->host().total_stats().media_unrouted, 0U);
}

TEST_F(ClientInputsTest, ChangingTheInputsWhileConnectedRebuildsOnlyTheChangedStrip) {
    start_session();
    ASSERT_TRUE(simulation_->client_command(0, cp::SetAppAudio{true}));
    join_and_approve();
    unmute_strips();
    const auto mono = strip(cp::InputSourceKind::kMicrophone);
    const auto app = strip(cp::InputSourceKind::kApplicationAudio);
    ASSERT_TRUE(mono);
    ASSERT_TRUE(app);
    ASSERT_EQ(mono->channel_count, 1);

    ASSERT_TRUE(simulation_->client_command(0, cp::SetInputChannels{2}));
    play(500ms);

    // マイクのストリップはステレオの別のストリップとしてミュートで作り直され、アプリ音声の
    // ストリップはミュートを解いたまま残る。
    const auto stereo = strip(cp::InputSourceKind::kMicrophone);
    ASSERT_TRUE(stereo);
    EXPECT_NE(stereo->entity_id, mono->entity_id);
    EXPECT_EQ(stereo->channel_count, 2);
    EXPECT_TRUE(stereo->channel_muted);
    const auto kept = strip(cp::InputSourceKind::kApplicationAudio);
    ASSERT_TRUE(kept);
    EXPECT_EQ(kept->entity_id, app->entity_id);
    EXPECT_FALSE(kept->channel_muted);

    // アプリ音声は途切れずに届き続け、作り直したマイクにも音が届く。
    const std::uint64_t begin = simulation_->now_samples();
    play(1000ms);
    const std::uint64_t end = simulation_->now_samples();
    EXPECT_TRUE(present(left_[kept->entity_id], begin, end, kAppHz));
    EXPECT_TRUE(present(left_[stereo->entity_id], begin, end, kMicLeftHz));
    EXPECT_TRUE(absent(right_[stereo->entity_id], begin, end, kMicLeftHz));

    // アプリ音声をやめると、そのストリップは片付き、マイクのストリップは残る。
    ASSERT_TRUE(simulation_->client_command(0, cp::SetAppAudio{false}));
    play(300ms);
    EXPECT_FALSE(strip(cp::InputSourceKind::kApplicationAudio));
    EXPECT_TRUE(strip(cp::InputSourceKind::kMicrophone));
    EXPECT_EQ(host_snapshot().channels.size(), 1U);
}

}  // namespace
}  // namespace vocroz::test_harness
