// SPDX-License-Identifier: AGPL-3.0-only
//
// 「親機 + 子機 3 台」の結合テスト（AT-A02、AT-A03、AT-A05 の CI で確かめられる部分）。
// session の HostSession と ClientSession を、transport の部品（Noise_IK、暗号化、信頼制御層、
// 死活監視、Opus、ジッターバッファ）とシミュレーションのネットワークでつなぎ、招待から参加、承認、
// 送信ミュートの解除、ミックス、切断と復帰までを、UI のコマンドだけで進める。
// - 承認前の子機の音は、子機がミュートを無視して送ってきても親機の出力に出ない
// - 3 台の音が独立にミックスされ、チャンネルと Master のミュートがそれぞれ効く
// - 1 台が切断しても残りの音は途切れず、戻った子機は承認なしで同じストリップに戻る

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
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

// 子機ごとの音の周波数。100 ms の窓にちょうど整数周期が入り、互いの倍音とも重ならない。
constexpr std::array<double, 3> kToneHz{440.0, 660.0, 880.0};
// 子機ごとの音の振幅。3 台を足しても 1 を超えない。
constexpr double kToneAmplitude = 0.2;
// 窓の中で「鳴っている」とみなす振幅の下限と、「鳴っていない」とみなす上限。
// Opus を通すと振幅は少し変わるので、送った振幅の半分を下限にする。
constexpr double kPresentAmplitude = kToneAmplitude / 2;
constexpr double kAbsentAmplitude = 0.01;
// 判定の窓の長さ。
constexpr std::uint64_t kWindow = ms(100);

// 出力を時刻つきで記録し、区間の周波数ごとの振幅を返す。
class OutputRecorder {
public:
    void record(std::uint64_t first, std::span<const float> block) {
        if (samples_.size() < first + block.size()) {
            samples_.resize(first + block.size());
        }
        std::copy(block.begin(), block.end(),
                  samples_.begin() + static_cast<std::ptrdiff_t>(first));
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

    // [begin, end) の絶対値の最大。
    [[nodiscard]] double peak(std::uint64_t begin, std::uint64_t end) const {
        double peak = 0.0;
        for (std::uint64_t i = begin; i < end && i < samples_.size(); ++i) {
            peak = std::max(peak, static_cast<double>(std::abs(samples_[i])));
        }
        return peak;
    }

private:
    std::vector<float> samples_;
};

class EnsembleTest : public ::testing::Test {
protected:
    void create(std::vector<bool> ignore_send_mute = {}) {
        EnsembleConfig config;
        config.client_names = {"Vo", "Gt", "Ba"};
        config.ignore_send_mute = std::move(ignore_send_mute);
        config.link = netsim::parse_fault_profile("delay=10ms,jitter=2ms,seed=7").value();
        simulation_ = std::make_unique<EnsembleSimulation>(std::move(config));
    }

    // 子機ごとの音を入力し、親機の出力を記録しながら duration だけ進める。
    void play(std::chrono::milliseconds duration) {
        simulation_->run(
            ms(static_cast<std::uint64_t>(duration.count())),
            [](std::size_t client, std::uint64_t first, std::span<float> block) {
                for (std::size_t i = 0; i < block.size(); ++i) {
                    const double t = static_cast<double>(first + i) /
                                     static_cast<double>(transport::kSampleRate);
                    block[i] = static_cast<float>(
                        kToneAmplitude * std::sin(2.0 * std::numbers::pi * kToneHz[client] * t));
                }
            },
            [this](std::uint64_t first, std::span<const float> block) {
                output_.record(first, block);
            });
    }

    cp::MixerSnapshot host_snapshot() {
        return simulation_->host().session().snapshot();
    }

    // セッションを作り、Master のミュートを解く。
    void start_session() {
        ASSERT_TRUE(
            simulation_->host_command(host_snapshot().session.entity_id, cp::CreateSession{}));
        ASSERT_TRUE(simulation_->invite());
        ASSERT_TRUE(
            simulation_->host_command(host_snapshot().master.entity_id, cp::SetMasterMute{false}));
    }

    // 子機 index が招待で参加を申請し、承認待ちまで進める。
    void join(std::size_t index) {
        ASSERT_TRUE(simulation_->client_command(index, cp::Join{*simulation_->invite()}));
        play(300ms);
        ASSERT_EQ(simulation_->client(index).session().snapshot().client_state,
                  cp::ClientState::kWaitingApproval);
    }

    // 承認待ちの name を承認する。
    void approve(const std::string& name) {
        for (const auto& pending : host_snapshot().pending) {
            if (pending.name == name) {
                ASSERT_TRUE(simulation_->host_command(pending.entity_id, cp::ApproveParticipant{}));
                return;
            }
        }
        FAIL() << "承認待ちにない: " << name;
    }

    std::optional<cp::ChannelStrip> strip(const std::string& name) {
        for (const auto& channel : host_snapshot().channels) {
            if (channel.name == name) {
                return channel;
            }
        }
        return std::nullopt;
    }

    // 子機 index の送信ミュートと、そのストリップのチャンネルミュートを解く。
    void unmute(std::size_t index, const std::string& name) {
        ASSERT_TRUE(simulation_->client_command(index, cp::SetSendMute{false}));
        const auto channel = strip(name);
        ASSERT_TRUE(channel);
        ASSERT_TRUE(simulation_->host_command(channel->entity_id, cp::SetChannelMute{false}));
    }

    // 3 台を参加させ、承認し、ミュートを解いて音が届くところまで進める。
    void connect_all() {
        start_session();
        const std::vector<std::string> names{"Vo", "Gt", "Ba"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            join(i);
            approve(names[i]);
        }
        play(300ms);
        for (std::size_t i = 0; i < names.size(); ++i) {
            ASSERT_EQ(simulation_->client(i).session().snapshot().client_state,
                      cp::ClientState::kConnected);
            unmute(i, names[i]);
        }
        play(500ms);
    }

    // [begin, end) を窓に分け、すべての窓で子機 client の音が鳴っていれば true。
    [[nodiscard]] bool present_throughout(std::size_t client, std::uint64_t begin,
                                          std::uint64_t end) const {
        for (std::uint64_t w = begin; w + kWindow <= end; w += kWindow) {
            if (output_.amplitude(w, w + kWindow, kToneHz[client]) < kPresentAmplitude) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool absent_throughout(std::size_t client, std::uint64_t begin,
                                         std::uint64_t end) const {
        for (std::uint64_t w = begin; w + kWindow <= end; w += kWindow) {
            if (output_.amplitude(w, w + kWindow, kToneHz[client]) > kAbsentAmplitude) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::uint64_t now() const {
        return simulation_->now_samples();
    }

    std::unique_ptr<EnsembleSimulation> simulation_;
    OutputRecorder output_;
};

// 承認前の子機の音は出力に出ない。送信ミュートを無視して送り続ける子機（Vo）でも、
// 承認されるまでは親機がメディアを捨てる。承認した子機の音だけがミックスに入る。
TEST_F(EnsembleTest, BlocksAudioUntilApproved) {
    create({true, false, false});
    start_session();
    for (std::size_t i = 0; i < 3; ++i) {
        join(i);
    }
    const auto waiting_from = now();
    play(1000ms);
    EXPECT_EQ(host_snapshot().pending.size(), 3U);
    EXPECT_TRUE(host_snapshot().channels.empty());
    EXPECT_GT(simulation_->host().total_stats().media_before_admission, 0U);
    EXPECT_EQ(output_.peak(waiting_from, now()), 0.0);

    // Gt と Ba だけを承認する。Vo は承認待ちのまま送り続ける。
    approve("Gt");
    approve("Ba");
    play(300ms);
    unmute(1, "Gt");
    unmute(2, "Ba");
    play(500ms);
    const auto mixing_from = now();
    play(1000ms);
    EXPECT_TRUE(present_throughout(1, mixing_from, now()));
    EXPECT_TRUE(present_throughout(2, mixing_from, now()));
    EXPECT_TRUE(absent_throughout(0, mixing_from, now()));
    EXPECT_EQ(host_snapshot().channels.size(), 2U);
    EXPECT_EQ(simulation_->host().mixer().channels().size(), 2U);
}

// 3 台の音がそれぞれのストリップを通って独立にミックスされ、
// チャンネルのミュートはその 1 台だけを、Master のミュートは全体を無音にする。
TEST_F(EnsembleTest, MixesThreeClientsAndAppliesChannelAndMasterMute) {
    create();
    connect_all();
    const auto all_from = now();
    play(1000ms);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(present_throughout(i, all_from, now())) << "子機 " << i;
    }

    const auto gt = strip("Gt");
    ASSERT_TRUE(gt);
    ASSERT_TRUE(simulation_->host_command(gt->entity_id, cp::SetChannelMute{true}));
    // ゲインランプが終わるまで待ってから判定する。
    play(100ms);
    const auto channel_muted_from = now();
    play(1000ms);
    EXPECT_TRUE(present_throughout(0, channel_muted_from, now()));
    EXPECT_TRUE(absent_throughout(1, channel_muted_from, now()));
    EXPECT_TRUE(present_throughout(2, channel_muted_from, now()));

    ASSERT_TRUE(
        simulation_->host_command(host_snapshot().master.entity_id, cp::SetMasterMute{true}));
    play(100ms);
    const auto master_muted_from = now();
    play(500ms);
    EXPECT_EQ(output_.peak(master_muted_from, now()), 0.0);
}

// 1 台（Gt）の回線が 7 秒途絶えても、残りの 2 台の音は途切れない。
// 回線が戻ると、Gt は再接続トークンで承認なしに同じストリップへ戻り、音も戻る。
TEST_F(EnsembleTest, KeepsOthersPlayingThroughDisconnectAndRejoinsSameStrip) {
    create();
    connect_all();
    const auto before = strip("Gt");
    ASSERT_TRUE(before);

    simulation_->disconnect(1);
    const auto outage_from = now();
    play(7000ms);
    const auto outage_to = now();
    EXPECT_TRUE(present_throughout(0, outage_from, outage_to));
    EXPECT_TRUE(present_throughout(2, outage_from, outage_to));
    // 途絶から NO_SIGNAL で無音になるまでの猶予（500 ms）を除いて、Gt の音は出ない。
    EXPECT_TRUE(absent_throughout(1, outage_from + ms(1000), outage_to));
    EXPECT_EQ(simulation_->client(1).session().snapshot().client_state,
              cp::ClientState::kReconnecting);
    const auto during = strip("Gt");
    ASSERT_TRUE(during);
    EXPECT_NE(during->connection_state, cp::ConnectionState::kConnected);

    simulation_->restore(1);
    play(1500ms);
    EXPECT_EQ(simulation_->client(1).session().snapshot().client_state,
              cp::ClientState::kConnected);
    EXPECT_EQ(simulation_->client(1).stats().links_established, 2U);
    const auto after = strip("Gt");
    ASSERT_TRUE(after);
    EXPECT_EQ(after->entity_id, before->entity_id);
    EXPECT_EQ(after->connection_state, cp::ConnectionState::kConnected);
    EXPECT_FALSE(after->channel_muted);
    EXPECT_EQ(host_snapshot().channels.size(), 3U);
    EXPECT_TRUE(host_snapshot().pending.empty());
    EXPECT_EQ(simulation_->host().mixer().channels().size(), 3U);

    const auto rejoined_from = now();
    play(1000ms);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(present_throughout(i, rejoined_from, now())) << "子機 " << i;
    }
}

}  // namespace
}  // namespace vocroz::test_harness
