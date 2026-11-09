// SPDX-License-Identifier: AGPL-3.0-only
//
// add_minus_me_returns() と返送タップの単体テスト。子機ごとの返送に自分の声が入らないこと
// （自分のストリップと、自分の声を含むバスを除くこと）、ポストフェーダーで取ること、
// 不正な記述でグラフを変えないことを、Mixer で実際に音を流して確かめる。

#include "vocroz/audio_core/minus_me.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"

#include "fakes/fake_audio_output.h"
#include "fakes/fake_audio_sink.h"
#include "fakes/fake_audio_source.h"

namespace vocroz::audio_core {
namespace {

// 既定のランプ（10ms）が確実に終わる長さ。
constexpr std::size_t kAfterRamp = 1024;
// 加算の丸め誤差の許容。
constexpr float kTolerance = 1e-5F;

class NullListener final : public AudioDeviceListener {
public:
    void on_device_lost() noexcept override {}
};

// 子機 3 台（A、B、C）のストリップを Master へまとめる親機のグラフ。
// ソース 1〜3 → ストリップ 11〜13（フェーダー 1〜3）→ Master 20（フェーダー 0）→ 出力 21。
class MinusMeTest : public ::testing::Test {
protected:
    static constexpr NodeId kStripA{11};
    static constexpr NodeId kStripB{12};
    static constexpr NodeId kStripC{13};

    MinusMeTest() {
        const float levels[] = {0.1F, 0.2F, 0.4F};
        for (std::uint32_t i = 0; i < 3; ++i) {
            const NodeId source{i + 1};
            const NodeId strip{11 + i};
            graph_.nodes.push_back(NodeSpec::make_source(
                source, std::make_shared<FakeAudioSource>(FakeAudioSource::constant(levels[i]))));
            graph_.nodes.push_back(NodeSpec::make_strip(
                strip, FaderId{static_cast<std::uint16_t>(i + 1)}, std::nullopt));
            graph_.connections.emplace_back(source, strip);
            graph_.connections.emplace_back(strip, NodeId{20});
        }
        graph_.nodes.push_back(NodeSpec::make_bus(NodeId{20}, FaderId{0}, std::nullopt));
        graph_.nodes.push_back(NodeSpec::make_output(NodeId{21}, std::nullopt));
        graph_.connections.emplace_back(NodeId{20}, NodeId{21});
    }

    void SetUp() override {
        ASSERT_TRUE(output_.start(mixer_, listener_).has_value());
    }

    std::vector<MinusMeReturn> returns_for_each_strip() {
        return {{{kStripA}, sink_a_}, {{kStripB}, sink_b_}, {{kStripC}, sink_c_}};
    }

    void unmute_strips() {
        for (std::uint16_t fader = 1; fader <= 3; ++fader) {
            ASSERT_TRUE(mixer_.set_fader_muted(FaderId{fader}, false).has_value());
        }
    }

    void render() {
        (void)output_.render_frames(kAfterRamp);
    }

    GraphDescription graph_;
    Mixer mixer_;
    FakeAudioOutput output_;
    NullListener listener_;
    std::shared_ptr<FakeAudioSink> sink_a_ = std::make_shared<FakeAudioSink>();
    std::shared_ptr<FakeAudioSink> sink_b_ = std::make_shared<FakeAudioSink>();
    std::shared_ptr<FakeAudioSink> sink_c_ = std::make_shared<FakeAudioSink>();
};

TEST_F(MinusMeTest, EachReturnContainsEveryoneButItsOwnStrip) {
    const std::vector<NodeId> strips = {kStripA, kStripB, kStripC};
    const auto returns = returns_for_each_strip();
    ASSERT_TRUE(add_minus_me_returns(graph_, strips, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());
    unmute_strips();

    render();

    EXPECT_NEAR(sink_a_->last(), 0.2F + 0.4F, kTolerance);
    EXPECT_NEAR(sink_b_->last(), 0.1F + 0.4F, kTolerance);
    EXPECT_NEAR(sink_c_->last(), 0.1F + 0.2F, kTolerance);
}

TEST_F(MinusMeTest, ReturnTapReceivesEveryRenderedSample) {
    const std::vector<NodeId> strips = {kStripA, kStripB, kStripC};
    const auto returns = returns_for_each_strip();
    ASSERT_TRUE(add_minus_me_returns(graph_, strips, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());

    // コールバック長がブロック長と違っても、返送は途切れずに渡る。
    (void)output_.render_frames(100);
    (void)output_.render_frames(300);

    EXPECT_EQ(sink_a_->received(), 400U);
}

TEST_F(MinusMeTest, ReturnFollowsChannelFadersBecauseItIsPostFader) {
    const std::vector<NodeId> strips = {kStripA, kStripB, kStripC};
    const auto returns = returns_for_each_strip();
    ASSERT_TRUE(add_minus_me_returns(graph_, strips, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());
    unmute_strips();
    // オペレーターが C をチャンネルミュートすると、ほかの子機の返送からも C が消える。
    ASSERT_TRUE(mixer_.set_fader_muted(FaderId{3}, true).has_value());

    render();

    EXPECT_NEAR(sink_a_->last(), 0.2F, kTolerance);
    EXPECT_NEAR(sink_b_->last(), 0.1F, kTolerance);
}

TEST_F(MinusMeTest, BusCarryingOwnVoiceIsExcludedFromThatReturnOnly) {
    // A をセンドでリバーブ用のバス 30 へ送り、バス 30 も返送の候補にする。
    graph_.nodes.push_back(NodeSpec::make_bus(NodeId{30}, FaderId{4}, std::nullopt));
    graph_.connections.emplace_back(kStripA, NodeId{30}, SendTap::kPostFader, FaderId{5});
    graph_.connections.emplace_back(NodeId{30}, NodeId{20});
    const std::vector<NodeId> candidates = {kStripA, kStripB, kStripC, NodeId{30}};
    const auto returns = returns_for_each_strip();
    ASSERT_TRUE(add_minus_me_returns(graph_, candidates, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());
    unmute_strips();
    ASSERT_TRUE(mixer_.set_fader_muted(FaderId{4}, false).has_value());
    ASSERT_TRUE(mixer_.set_fader_muted(FaderId{5}, false).has_value());

    render();

    // A の返送には、A の声を含むバスも入らない。
    EXPECT_NEAR(sink_a_->last(), 0.2F + 0.4F, kTolerance);
    // B の返送には、ストリップの A とバス経由の A の両方が入る。
    EXPECT_NEAR(sink_b_->last(), 0.1F + 0.1F + 0.4F, kTolerance);
}

TEST_F(MinusMeTest, ReturnWithSeveralOwnInputsExcludesAllOfThem) {
    // 1 台の子機が A と B の 2 本のストリップを持つ場合。
    const std::vector<NodeId> strips = {kStripA, kStripB, kStripC};
    const std::vector<MinusMeReturn> returns = {{{kStripA, kStripB}, sink_a_}};
    ASSERT_TRUE(add_minus_me_returns(graph_, strips, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());
    unmute_strips();

    render();

    EXPECT_NEAR(sink_a_->last(), 0.4F, kTolerance);
}

TEST_F(MinusMeTest, DuplicateCandidatesAreConnectedOnce) {
    const std::vector<NodeId> candidates = {kStripB, kStripB};
    const std::vector<MinusMeReturn> returns = {{{kStripA}, sink_a_}};
    ASSERT_TRUE(add_minus_me_returns(graph_, candidates, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());
    unmute_strips();

    render();

    EXPECT_NEAR(sink_a_->last(), 0.2F, kTolerance);
}

TEST_F(MinusMeTest, ReturnsStaySilentWhileChannelsAreMuted) {
    // 初期ミュート: チャンネルのフェーダーはミュートで始まるので、返送も無音で始まる。
    const std::vector<NodeId> strips = {kStripA, kStripB, kStripC};
    const auto returns = returns_for_each_strip();
    ASSERT_TRUE(add_minus_me_returns(graph_, strips, returns).has_value());
    ASSERT_TRUE(mixer_.apply_graph(graph_).has_value());

    render();

    for (const float sample : sink_a_->samples()) {
        ASSERT_EQ(sample, 0.0F);
    }
}

TEST_F(MinusMeTest, AddedNodesUseUnusedIdsAndReportTheTaps) {
    const std::vector<NodeId> strips = {kStripA, kStripB};
    const std::vector<MinusMeReturn> returns = {{{kStripA}, sink_a_}, {{kStripB}, sink_b_}};
    const auto report = add_minus_me_returns(graph_, strips, returns);
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report.value().taps.size(), 2U);
    // 既存のノードの最大値（21）の次から振る。
    EXPECT_GT(report.value().taps[0].value, 21U);
    EXPECT_NE(report.value().taps[0], report.value().taps[1]);
    EXPECT_TRUE(mixer_.validate_graph(graph_).has_value());
}

TEST_F(MinusMeTest, RejectsUnknownNodesWithoutChangingTheGraph) {
    const auto before = graph_.nodes.size();
    const std::vector<NodeId> strips = {kStripA, NodeId{99}};
    const std::vector<MinusMeReturn> returns = {{{kStripB}, sink_b_}};
    const auto result = add_minus_me_returns(graph_, strips, returns);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), GraphError::kUnknownNode);

    const std::vector<NodeId> valid = {kStripA};
    const std::vector<MinusMeReturn> unknown_own = {{{NodeId{98}}, sink_b_}};
    const auto own_result = add_minus_me_returns(graph_, valid, unknown_own);
    ASSERT_FALSE(own_result.has_value());
    EXPECT_EQ(own_result.error(), GraphError::kUnknownNode);
    EXPECT_EQ(graph_.nodes.size(), before);
}

TEST_F(MinusMeTest, RejectsReturnWithoutSink) {
    const auto before = graph_.nodes.size();
    const std::vector<NodeId> strips = {kStripA};
    const std::vector<MinusMeReturn> returns = {{{kStripB}, nullptr}};
    const auto result = add_minus_me_returns(graph_, strips, returns);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), GraphError::kMissingSink);
    EXPECT_EQ(graph_.nodes.size(), before);
}

TEST_F(MinusMeTest, ValidatorRejectsReturnTapWithoutSinkOrAsSendSource) {
    GraphDescription missing_sink = graph_;
    missing_sink.nodes.push_back(NodeSpec::make_return_tap(NodeId{40}, nullptr));
    ASSERT_FALSE(mixer_.validate_graph(missing_sink).has_value());
    EXPECT_EQ(mixer_.validate_graph(missing_sink).error(), GraphError::kMissingSink);

    // 返送タップは出力を持たないので、後段へつなげない。
    GraphDescription tap_as_source = graph_;
    tap_as_source.nodes.push_back(NodeSpec::make_return_tap(NodeId{40}, sink_a_));
    tap_as_source.connections.emplace_back(kStripA, NodeId{40});
    tap_as_source.connections.emplace_back(NodeId{40}, NodeId{20});
    ASSERT_FALSE(mixer_.validate_graph(tap_as_source).has_value());
    EXPECT_EQ(mixer_.validate_graph(tap_as_source).error(), GraphError::kInvalidConnection);
}

}  // namespace
}  // namespace vocroz::audio_core
