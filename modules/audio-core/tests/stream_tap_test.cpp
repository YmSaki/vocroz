// SPDX-License-Identifier: AGPL-3.0-only
//
// ストリームタップ（NodeKind::kStreamTap）の単体テスト。SRTP 外部出力が送り出すバスの
// ポストフェーダーの音をステレオのまま受け取ること、モノラルの経路は L と R に同じ音が
// 入ること、不正な記述を検証で弾くことを、Mixer で実際に音を流して確かめる
// （docs/adr/0040-srtp-external-output.md）。

#include <memory>

#include <gtest/gtest.h>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/mixer.h"

#include "fakes/fake_audio_output.h"
#include "fakes/fake_audio_source.h"
#include "fakes/fake_audio_stream_sink.h"

namespace vocroz::audio_core {
namespace {

// 既定のランプ（10ms）が確実に終わる長さ。
constexpr std::size_t kAfterRamp = 1024;
// 加算の丸め誤差の許容。
constexpr float kTolerance = 1e-5F;

constexpr NodeId kSource{1};
constexpr NodeId kStrip{2};
constexpr NodeId kBus{3};
constexpr NodeId kOutput{4};
constexpr NodeId kTap{5};
constexpr FaderId kStripFader{1};
constexpr FaderId kBusFader{2};

class NullListener final : public AudioDeviceListener {
public:
    void on_device_lost() noexcept override {}
};

// ソース → ストリップ → バス → 出力 のグラフに、バスのポストフェーダーからストリームタップを足す。
class StreamTapTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(output_.start(mixer_, listener_).has_value());
    }

    GraphDescription make_graph(FakeAudioSource source, std::size_t strip_channels) {
        GraphDescription graph;
        graph.nodes.push_back(
            NodeSpec::make_source(kSource, std::make_shared<FakeAudioSource>(source)));
        graph.nodes.push_back(
            NodeSpec::make_strip(kStrip, kStripFader, std::nullopt, 0, strip_channels));
        graph.nodes.push_back(NodeSpec::make_bus(kBus, kBusFader, std::nullopt));
        graph.nodes.push_back(NodeSpec::make_output(kOutput, std::nullopt));
        graph.nodes.push_back(NodeSpec::make_stream_tap(kTap, sink_));
        graph.connections.emplace_back(kSource, kStrip);
        graph.connections.emplace_back(kStrip, kBus);
        graph.connections.emplace_back(kBus, kOutput);
        graph.connections.emplace_back(kBus, kTap);
        return graph;
    }

    void unmute() {
        ASSERT_TRUE(mixer_.set_fader_muted(kStripFader, false).has_value());
        ASSERT_TRUE(mixer_.set_fader_muted(kBusFader, false).has_value());
    }

    void render() {
        (void)output_.render_frames(kAfterRamp);
    }

    Mixer mixer_;
    FakeAudioOutput output_;
    NullListener listener_;
    std::shared_ptr<FakeAudioStreamSink> sink_ = std::make_shared<FakeAudioStreamSink>();
};

TEST_F(StreamTapTest, ReceivesStereoBusOutputPostFader) {
    ASSERT_TRUE(mixer_.apply_graph(make_graph(FakeAudioSource::stereo(0.2F, 0.4F), 2)).has_value());
    unmute();
    render();

    EXPECT_EQ(sink_->received(), kAfterRamp);
    EXPECT_NEAR(sink_->last_left(), 0.2F, kTolerance);
    EXPECT_NEAR(sink_->last_right(), 0.4F, kTolerance);
}

TEST_F(StreamTapTest, MonoPathIsDuplicatedToBothChannels) {
    ASSERT_TRUE(mixer_.apply_graph(make_graph(FakeAudioSource::constant(0.3F), 1)).has_value());
    unmute();
    render();

    EXPECT_NEAR(sink_->last_left(), sink_->last_right(), kTolerance);
    EXPECT_GT(sink_->last_left(), 0.0F);
}

TEST_F(StreamTapTest, FollowsBusFaderMute) {
    ASSERT_TRUE(mixer_.apply_graph(make_graph(FakeAudioSource::stereo(0.2F, 0.4F), 2)).has_value());
    // 初期ミュートのままなので、送り出す音も無音になる。
    render();

    EXPECT_EQ(sink_->received(), kAfterRamp);
    EXPECT_FLOAT_EQ(sink_->last_left(), 0.0F);
    EXPECT_FLOAT_EQ(sink_->last_right(), 0.0F);
}

TEST_F(StreamTapTest, ValidatorRejectsTapWithoutSinkOrAsSendSource) {
    GraphDescription missing_sink = make_graph(FakeAudioSource::constant(0.1F), 1);
    missing_sink.nodes.back() = NodeSpec::make_stream_tap(kTap, nullptr);
    ASSERT_FALSE(mixer_.validate_graph(missing_sink).has_value());
    EXPECT_EQ(mixer_.validate_graph(missing_sink).error(), GraphError::kMissingSink);

    // ストリームタップは出力を持たないので、後段へつなげない。
    GraphDescription tap_as_source = make_graph(FakeAudioSource::constant(0.1F), 1);
    tap_as_source.connections.emplace_back(kTap, kOutput);
    ASSERT_FALSE(mixer_.validate_graph(tap_as_source).has_value());
    EXPECT_EQ(mixer_.validate_graph(tap_as_source).error(), GraphError::kInvalidConnection);
}

}  // namespace
}  // namespace vocroz::audio_core
