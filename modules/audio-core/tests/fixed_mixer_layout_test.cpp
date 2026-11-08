// SPDX-License-Identifier: AGPL-3.0-only
//
// FixedMixerLayout の単体テスト。識別子の割り当てと、記述したグラフが検証を通ることを確かめる。

#include "vocroz/audio_core/fixed_mixer_layout.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_audio_source.h"
#include "render_graph.h"

namespace vocroz::audio_core {
namespace {

TEST(FixedMixerLayoutTest, AssignsChannelIdsThenMaster) {
    const FixedMixerLayout layout(8);
    EXPECT_EQ(layout.channel_fader(0), FaderId{0});
    EXPECT_EQ(layout.channel_meter(7), MeterId{7});
    EXPECT_EQ(layout.master_fader(), FaderId{8});
    EXPECT_EQ(layout.master_meter(), MeterId{8});
    EXPECT_EQ(layout.required_capacity(), 9U);
}

TEST(FixedMixerLayoutTest, DescribesValidGraphWithAndWithoutSources) {
    const FixedMixerLayout layout(8);
    std::vector<std::shared_ptr<AudioSource>> sources(3);
    sources[1] = std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.1F));

    const GraphDescription graph = layout.describe(sources);
    // ストリップ 8 + ソース 1 + Master バス、リミッター、出力。
    EXPECT_EQ(graph.nodes.size(), 12U);
    EXPECT_TRUE(RenderGraph::build(graph, layout.required_capacity(), layout.required_capacity())
                    .has_value());
}

TEST(FixedMixerLayoutTest, TreatsZeroChannelsAsOne) {
    const FixedMixerLayout layout(0);
    EXPECT_EQ(layout.channel_count(), 1U);
}

}  // namespace
}  // namespace vocroz::audio_core
