// SPDX-License-Identifier: AGPL-3.0-only
//
// Mixer の単体テスト。FakeAudioOutput でデバイスのコールバックを模擬し、
// 初期ミュート、ゲインランプ、安全ミュート（SAFETY MUTE）、グラフのスワップ、
// 可変のコールバック長、出力の安全性を検証する。

#include "vocroz/audio_core/mixer.h"

#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/fixed_mixer_layout.h"

#include "fakes/fake_audio_output.h"
#include "fakes/fake_audio_source.h"

namespace vocroz::audio_core {
namespace {

// -1 dBFS（安全リミッターの天井）。
constexpr float kCeiling = 0.891F;
// 既定のランプ（10ms）が確実に終わる長さ。
constexpr std::size_t kAfterRamp = 1024;

// 出力デバイスの抜去を受けて安全ミュートをかける、session の役割を模したリスナー。
class SafetyMuteOnDeviceLost final : public AudioDeviceListener {
public:
    explicit SafetyMuteOnDeviceLost(Mixer& mixer) : mixer_(mixer) {}
    void on_device_lost() noexcept override {
        mixer_.engage_safety_mute();
    }

private:
    Mixer& mixer_;
};

class MixerTest : public ::testing::Test {
protected:
    static constexpr std::size_t kChannels = 8;

    MixerTest() : listener_(mixer_) {
        for (std::size_t ch = 0; ch < kChannels; ++ch) {
            sources_.push_back(std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.1F)));
        }
    }

    void SetUp() override {
        ASSERT_TRUE(mixer_.apply_graph(layout_.describe(sources_)).has_value());
        ASSERT_TRUE(output_.start(mixer_, listener_).has_value());
    }

    void unmute_channel_and_master(std::size_t channel) {
        ASSERT_TRUE(mixer_.set_fader_muted(layout_.channel_fader(channel), false).has_value());
        ASSERT_TRUE(mixer_.set_fader_muted(layout_.master_fader(), false).has_value());
    }

    // 1 回のコールバックの、最後のフレームの L を返す。
    float last_left(std::size_t frames) {
        const std::vector<float> out = output_.render_frames(frames);
        return out[out.size() - 2];
    }

    FixedMixerLayout layout_{kChannels};
    Mixer mixer_;
    SafetyMuteOnDeviceLost listener_;
    FakeAudioOutput output_{2};
    std::vector<std::shared_ptr<AudioSource>> sources_;
};

TEST_F(MixerTest, OutputsSilenceBeforeAnyGraph) {
    Mixer mixer;
    FakeAudioOutput output;
    SafetyMuteOnDeviceLost listener(mixer);
    ASSERT_TRUE(output.start(mixer, listener).has_value());
    EXPECT_EQ(output.render_frames(256), std::vector<float>(512, 0.0F));
}

TEST_F(MixerTest, StartsWithAllChannelsAndMasterMuted) {
    // 入力が届いていても、オペレーターが解除するまで音を出さない（初期ミュート）。
    EXPECT_EQ(output_.render_frames(kAfterRamp), std::vector<float>(kAfterRamp * 2, 0.0F));
    // プリフェーダーのメーターは入力を測っている。
    EXPECT_FLOAT_EQ(mixer_.read_meter(layout_.channel_meter(0)).peak, 0.1F);
    EXPECT_EQ(mixer_.read_meter(layout_.master_meter()).peak, 0.0F);
}

TEST_F(MixerTest, UnmutingOnlyChannelOrOnlyMasterStaysSilent) {
    ASSERT_TRUE(mixer_.set_fader_muted(layout_.channel_fader(0), false).has_value());
    EXPECT_EQ(last_left(kAfterRamp), 0.0F);
    ASSERT_TRUE(mixer_.set_fader_muted(layout_.channel_fader(0), true).has_value());
    ASSERT_TRUE(mixer_.set_fader_muted(layout_.master_fader(), false).has_value());
    EXPECT_EQ(last_left(kAfterRamp), 0.0F);
}

TEST_F(MixerTest, UnmuteRampsInWithoutStep) {
    unmute_channel_and_master(0);
    const std::vector<float> out = output_.render_frames(kAfterRamp);
    // 最初のサンプルは無音で、10ms（480 サンプル）以上かけて目標値に達する。
    EXPECT_EQ(out[0], 0.0F);
    EXPECT_LT(out[2 * 240], 0.1F);
    for (std::size_t i = 1; i < kAfterRamp; ++i) {
        EXPECT_LE(out[2 * (i - 1)], out[2 * i] + 1e-7F);
    }
    EXPECT_FLOAT_EQ(out[2 * (kAfterRamp - 1)], 0.1F);
}

TEST_F(MixerTest, SumsEightChannelsIntoMaster) {
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        ASSERT_TRUE(mixer_.set_fader_muted(layout_.channel_fader(ch), false).has_value());
    }
    ASSERT_TRUE(mixer_.set_fader_muted(layout_.master_fader(), false).has_value());
    ASSERT_TRUE(mixer_.set_fader_gain_db(layout_.master_fader(), -20.0F).has_value());
    // 0.1 × 8 チャンネル × -20 dB（0.1）= 0.08。リミッターの天井より小さいので変わらない。
    EXPECT_NEAR(last_left(kAfterRamp), 0.08F, 1e-5F);
}

TEST_F(MixerTest, AppliesFaderGainInDb) {
    unmute_channel_and_master(0);
    ASSERT_TRUE(mixer_.set_fader_gain_db(layout_.channel_fader(0), 6.0206F).has_value());
    EXPECT_NEAR(last_left(kAfterRamp), 0.2F, 1e-4F);
    ASSERT_TRUE(
        mixer_.set_fader_gain_db(layout_.channel_fader(0), -std::numeric_limits<float>::infinity())
            .has_value());
    EXPECT_EQ(last_left(kAfterRamp), 0.0F);
}

TEST_F(MixerTest, LimitsMasterOutputBelowCeiling) {
    unmute_channel_and_master(0);
    for (std::size_t ch = 1; ch < kChannels; ++ch) {
        ASSERT_TRUE(mixer_.set_fader_muted(layout_.channel_fader(ch), false).has_value());
    }
    ASSERT_TRUE(mixer_.set_fader_gain_db(layout_.master_fader(), 10.0F).has_value());
    // 0.1 × 8 × +10 dB は約 2.5 になるが、安全リミッターで天井に収まる。
    const std::vector<float> out = output_.render_frames(4096);
    for (const float sample : out) {
        EXPECT_LE(std::fabs(sample), kCeiling + 1e-6F);
    }
    EXPECT_NEAR(mixer_.read_meter(layout_.master_meter()).peak, kCeiling, 1e-3F);
}

TEST_F(MixerTest, DeviceLostEngagesSafetyMuteImmediately) {
    unmute_channel_and_master(0);
    ASSERT_GT(last_left(kAfterRamp), 0.0F);

    output_.simulate_device_lost();
    EXPECT_TRUE(mixer_.is_safety_muted());

    // 新しいデバイスで再開しても、オペレーターが解除するまでは最初のサンプルから無音。
    FakeAudioOutput replacement;
    ASSERT_TRUE(replacement.start(mixer_, listener_).has_value());
    EXPECT_EQ(replacement.render_frames(kAfterRamp), std::vector<float>(kAfterRamp * 2, 0.0F));

    // 解除はランプで戻る（クリックノイズを出さない）。
    mixer_.release_safety_mute();
    EXPECT_FALSE(mixer_.is_safety_muted());
    const std::vector<float> resumed = replacement.render_frames(kAfterRamp);
    EXPECT_EQ(resumed[0], 0.0F);
    EXPECT_FLOAT_EQ(resumed[2 * (kAfterRamp - 1)], 0.1F);
}

TEST_F(MixerTest, SafetyMuteKeepsFaderStateUnchanged) {
    unmute_channel_and_master(0);
    (void)output_.render_frames(kAfterRamp);
    mixer_.engage_safety_mute();
    EXPECT_EQ(last_left(kAfterRamp), 0.0F);
    // 安全ミュートの間もメーターは入力を測り続ける。
    EXPECT_FLOAT_EQ(mixer_.read_meter(layout_.channel_meter(0)).peak, 0.1F);
}

TEST_F(MixerTest, ResetFaderSilencesNewParticipantImmediately) {
    unmute_channel_and_master(0);
    ASSERT_GT(last_left(kAfterRamp), 0.0F);
    ASSERT_TRUE(mixer_.reset_fader(layout_.channel_fader(0)).has_value());
    const std::vector<float> out = output_.render_frames(kBlockSize);
    EXPECT_EQ(out[0], 0.0F);
}

TEST_F(MixerTest, GraphSwapKeepsFaderStateAndReleasesOldSources) {
    unmute_channel_and_master(0);
    ASSERT_FLOAT_EQ(last_left(kAfterRamp), 0.1F);

    // チャンネル 0 のソースを差し替える。フェーダーの状態はグラフの外にあるので、
    // スワップ直後のサンプルからミュート解除のまま新しいソースの音が出る。
    std::weak_ptr<AudioSource> old_source = sources_[0];
    sources_[0] = std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.3F));
    ASSERT_TRUE(mixer_.apply_graph(layout_.describe(sources_)).has_value());
    EXPECT_FALSE(mixer_.is_graph_settled());

    const std::vector<float> out = output_.render_frames(kBlockSize);
    EXPECT_FLOAT_EQ(out[0], 0.3F);
    // 古いグラフはオーディオスレッドでは破棄せず、コントロールスレッドが破棄する。
    EXPECT_FALSE(old_source.expired());
    EXPECT_TRUE(mixer_.is_graph_settled());
    EXPECT_TRUE(old_source.expired());
}

TEST_F(MixerTest, InvalidGraphKeepsCurrentGraph) {
    unmute_channel_and_master(0);
    GraphDescription invalid = layout_.describe(sources_);
    invalid.connections.push_back({invalid.connections.front().to, invalid.connections.front().to});
    EXPECT_EQ(mixer_.apply_graph(invalid).error(), GraphError::kCycle);
    EXPECT_FLOAT_EQ(last_left(kAfterRamp), 0.1F);
}

TEST_F(MixerTest, ProducesSameOutputForAnyCallbackLength) {
    // 正弦波を使い、ブロックの境界でサンプルが欠けたり重なったりしないことを確かめる。
    auto run = [&](std::vector<std::size_t> lengths) {
        Mixer mixer;
        FakeAudioOutput output;
        SafetyMuteOnDeviceLost listener(mixer);
        const FixedMixerLayout layout(1);
        std::vector<std::shared_ptr<AudioSource>> sources{
            std::make_shared<FakeAudioSource>(FakeAudioSource::sine(0.5F, 997.0F))};
        EXPECT_TRUE(mixer.apply_graph(layout.describe(sources)).has_value());
        EXPECT_TRUE(mixer.set_fader_muted(layout.channel_fader(0), false).has_value());
        EXPECT_TRUE(mixer.set_fader_muted(layout.master_fader(), false).has_value());
        EXPECT_TRUE(output.start(mixer, listener).has_value());
        std::vector<float> all;
        for (const std::size_t frames : lengths) {
            const std::vector<float> out = output.render_frames(frames);
            all.insert(all.end(), out.begin(), out.end());
        }
        return all;
    };
    const std::vector<float> reference = run({1536});
    EXPECT_EQ(run({64, 64, 128, 512, 256, 512}), reference);
    EXPECT_EQ(run({1, 127, 300, 441, 667}), reference);
}

TEST_F(MixerTest, WritesAverageToMonoDeviceAndSilenceToExtraChannels) {
    unmute_channel_and_master(0);
    (void)output_.render_frames(kAfterRamp);

    FakeAudioOutput mono(1);
    output_.stop();
    ASSERT_TRUE(mono.start(mixer_, listener_).has_value());
    const std::vector<float> mono_out = mono.render_frames(16);
    EXPECT_FLOAT_EQ(mono_out[15], 0.1F);
    mono.stop();

    FakeAudioOutput surround(6);
    ASSERT_TRUE(surround.start(mixer_, listener_).has_value());
    const std::vector<float> out = surround.render_frames(16);
    for (std::size_t frame = 0; frame < 16; ++frame) {
        EXPECT_FLOAT_EQ(out[frame * 6], 0.1F);
        EXPECT_FLOAT_EQ(out[frame * 6 + 1], 0.1F);
        for (std::size_t ch = 2; ch < 6; ++ch) {
            EXPECT_EQ(out[frame * 6 + ch], 0.0F);
        }
    }
}

// ソース → ストリップ → Main バス → 出力（チャンネル 0-1）と、
// ストリップ → プリフェーダー（センドレベル）→ モニターバス → 出力（channel から 2 チャンネル）。
GraphDescription main_and_monitor(std::shared_ptr<AudioSource> source, std::size_t channel) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, std::move(source)),
                   NodeSpec::make_strip(NodeId{2}, FaderId{0}, std::nullopt),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt),
                   NodeSpec::make_output(NodeId{4}, std::nullopt, 0),
                   NodeSpec::make_bus(NodeId{5}, FaderId{2}, std::nullopt),
                   NodeSpec::make_output(NodeId{6}, std::nullopt, channel)};
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{4}},
                         {NodeId{2}, NodeId{5}, SendTap::kPreFader, FaderId{3}},
                         {NodeId{5}, NodeId{6}}};
    return graph;
}

TEST(MixerRoutingTest, WritesEachOutputNodeToItsDeviceChannels) {
    Mixer mixer;
    FakeAudioOutput output(6);
    SafetyMuteOnDeviceLost listener(mixer);
    ASSERT_TRUE(mixer
                    .apply_graph(main_and_monitor(
                        std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.2F)), 4))
                    .has_value());
    for (const std::uint16_t id :
         {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{3}}) {
        ASSERT_TRUE(mixer.set_fader_muted(FaderId{id}, false).has_value());
    }
    ASSERT_TRUE(mixer.set_fader_gain_db(FaderId{0}, -6.0F).has_value());  // Main だけ下げる
    ASSERT_TRUE(output.start(mixer, listener).has_value());
    (void)output.render_frames(kAfterRamp);
    const std::vector<float> out = output.render_frames(16);
    const float main_level = 0.2F * std::pow(10.0F, -6.0F / 20.0F);
    for (std::size_t frame = 0; frame < 16; ++frame) {
        EXPECT_NEAR(out[frame * 6], main_level, 1e-5F);
        EXPECT_NEAR(out[frame * 6 + 1], main_level, 1e-5F);
        EXPECT_EQ(out[frame * 6 + 2], 0.0F);  // どの出力ノードも書かないチャンネル
        EXPECT_EQ(out[frame * 6 + 3], 0.0F);
        EXPECT_FLOAT_EQ(out[frame * 6 + 4], 0.2F);  // モニター（プリフェーダー）
        EXPECT_FLOAT_EQ(out[frame * 6 + 5], 0.2F);
    }
}

TEST(MixerRoutingTest, DropsOutputBeyondDeviceChannelsWithoutFallback) {
    // モニターの出力先がデバイスにないとき、Main のチャンネルへ振り替えて混ぜたりしない。
    Mixer mixer;
    FakeAudioOutput output(2);
    SafetyMuteOnDeviceLost listener(mixer);
    ASSERT_TRUE(mixer
                    .apply_graph(main_and_monitor(
                        std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.2F)), 2))
                    .has_value());
    ASSERT_TRUE(mixer.set_fader_muted(FaderId{2}, false).has_value());
    ASSERT_TRUE(mixer.set_fader_muted(FaderId{3}, false).has_value());
    ASSERT_TRUE(output.start(mixer, listener).has_value());
    (void)output.render_frames(kAfterRamp);
    const std::vector<float> out = output.render_frames(16);
    for (const float sample : out) {
        EXPECT_EQ(sample, 0.0F);
    }
}

TEST(MixerRoutingTest, ValidatesGraphWithoutApplyingIt) {
    Mixer mixer;
    GraphDescription loop =
        main_and_monitor(std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.2F)), 2);
    loop.nodes.push_back(NodeSpec::make_delay(NodeId{7}, kMinFeedbackDelaySamples));
    loop.connections.push_back({NodeId{5}, NodeId{7}});
    loop.connections.push_back({NodeId{7}, NodeId{5}});
    EXPECT_EQ(mixer.validate_graph(loop).error(), GraphError::kUnapprovedFeedback);
    loop.feedback_approved = true;
    EXPECT_TRUE(mixer.validate_graph(loop).has_value());
}

TEST_F(MixerTest, RejectsOutOfRangeFaderAndReportsFullQueue) {
    EXPECT_EQ(mixer_.set_fader_muted(FaderId{64}, false).error(), MixerError::kFaderOutOfRange);
    bool reported_full = false;
    for (int i = 0; i < 1000 && !reported_full; ++i) {
        const auto result = mixer_.set_fader_gain_db(layout_.channel_fader(0), -1.0F);
        reported_full = !result.has_value() && result.error() == MixerError::kQueueFull;
    }
    EXPECT_TRUE(reported_full);
    // オーディオスレッドが取り出せば、また積める。
    (void)output_.render_frames(kBlockSize);
    EXPECT_TRUE(mixer_.set_fader_gain_db(layout_.channel_fader(0), -1.0F).has_value());
}

}  // namespace
}  // namespace vocroz::audio_core
