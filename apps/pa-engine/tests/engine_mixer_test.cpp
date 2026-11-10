// SPDX-License-Identifier: AGPL-3.0-only
//
// EngineMixer の単体テスト。本物の Mixer を使い、render() の出力で初期ミュート、
// ミュートの解除、ルーティングの適用、受信ストリームの接続、メーターの読み取りを確かめる。

#include "engine_mixer.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"

#include "engine_constants.h"
#include "fakes/fake_audio_sink.h"
#include "fakes/fake_audio_source.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_midi_source.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

// ゲインランプ（10ms = 480 サンプル）を確実に越えるフレーム数。
constexpr std::size_t kAfterRamp = 1024;
constexpr float kInput = 0.1F;

class EngineMixerTest : public ::testing::Test {
protected:
    EngineMixerTest()
        : mixer_(ac::MixerConfig{kMixerFaderCapacity, kMixerMeterCapacity}),
          engine_(mixer_, logger_) {}

    // 1 回のコールバックぶんを描画し、最後のフレームの L を返す。
    float render_last_left() {
        std::vector<float> out(kAfterRamp * 2, 0.0F);
        mixer_.render(ac::InterleavedBuffer{out, 2});
        engine_.collect();
        return out[out.size() - 2];
    }

    std::shared_ptr<ac::AudioSource> constant_source() {
        return std::make_shared<ac::FakeAudioSource>(ac::FakeAudioSource::constant(kInput));
    }

    // ストリップを追加して音源をつなぎ、ストリップと Master のミュートを解く。
    void add_audible_strip(const std::string& id) {
        engine_.add_channel(id);
        ASSERT_TRUE(engine_.attach_source(id, constant_source()));
        engine_.set_channel_muted(id, false);
        engine_.set_master_muted(false);
    }

    // ch-1 →（プリフェーダー）→ bus-1 → Master。
    static cp::RoutingConfig prefader_send_through_bus() {
        cp::RoutingConfig config;
        config.buses.push_back(cp::RoutingBus{"bus-1", "A"});
        config.sends.push_back(cp::RoutingSend{"send-1", "ch-1", "bus-1", cp::SendTap::kPreFader});
        config.sends.push_back(
            cp::RoutingSend{"send-2", "bus-1", "master", cp::SendTap::kPostFader});
        return config;
    }

    common::FakeLogger logger_;
    ac::Mixer mixer_;
    EngineMixer engine_;
};

TEST_F(EngineMixerTest, NewStripWithSourceStaysSilentUntilUnmuted) {
    engine_.add_channel("ch-1");
    ASSERT_TRUE(engine_.attach_source("ch-1", constant_source()));
    // チャンネルと Master はミュートで始まる（初期ミュート）。
    EXPECT_EQ(render_last_left(), 0.0F);
    engine_.set_channel_muted("ch-1", false);
    EXPECT_EQ(render_last_left(), 0.0F);
    engine_.set_master_muted(false);
    EXPECT_GT(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, MutingChannelOrMasterSilencesOutput) {
    add_audible_strip("ch-1");
    ASSERT_GT(render_last_left(), 0.0F);
    engine_.set_channel_muted("ch-1", true);
    EXPECT_EQ(render_last_left(), 0.0F);
    engine_.set_channel_muted("ch-1", false);
    ASSERT_GT(render_last_left(), 0.0F);
    engine_.set_master_muted(true);
    EXPECT_EQ(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, ReaddedStripStartsMutedAgain) {
    add_audible_strip("ch-1");
    ASSERT_GT(render_last_left(), 0.0F);
    engine_.remove_channel("ch-1");
    EXPECT_EQ(render_last_left(), 0.0F);
    // 返したフェーダーを次のストリップが使っても、前の値（ミュート解除）を引き継がない。
    engine_.add_channel("ch-2");
    ASSERT_TRUE(engine_.attach_source("ch-2", constant_source()));
    EXPECT_EQ(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, DetachSourceSilencesStrip) {
    add_audible_strip("ch-1");
    ASSERT_GT(render_last_left(), 0.0F);
    engine_.detach_source("ch-1");
    EXPECT_EQ(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, AttachSourceFailsForUnknownStrip) {
    EXPECT_FALSE(engine_.attach_source("ch-unknown", constant_source()));
}

TEST_F(EngineMixerTest, MasterUnmuteReleasesSafetyMute) {
    add_audible_strip("ch-1");
    mixer_.engage_safety_mute();
    EXPECT_EQ(render_last_left(), 0.0F);
    engine_.set_master_muted(false);
    EXPECT_FALSE(mixer_.is_safety_muted());
    EXPECT_GT(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, NewSendStartsMutedAndUnmutesOnRequest) {
    engine_.add_channel("ch-1");
    ASSERT_TRUE(engine_.attach_source("ch-1", constant_source()));
    engine_.set_master_muted(false);
    // ストリップはミュートのまま、プリフェーダーのセンドでバスに送り、バスから Master に返す。
    // ストリップから Master への結線は暗黙にあるので、センドには書けない。
    const auto config = prefader_send_through_bus();
    ASSERT_TRUE(engine_.apply_routing(config, false, {}));
    EXPECT_EQ(render_last_left(), 0.0F);

    engine_.set_route_muted("send-1", false);
    engine_.set_route_muted("send-2", false);
    EXPECT_EQ(render_last_left(), 0.0F);
    // バスもミュートで始まる。
    engine_.set_route_muted("bus-1", false);
    EXPECT_GT(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, ReappliedSendKeepsItsFaderState) {
    engine_.add_channel("ch-1");
    ASSERT_TRUE(engine_.attach_source("ch-1", constant_source()));
    engine_.set_master_muted(false);
    auto config = prefader_send_through_bus();
    ASSERT_TRUE(engine_.apply_routing(config, false, {}));
    for (const char* id : {"send-1", "send-2", "bus-1"}) {
        engine_.set_route_muted(id, false);
    }
    ASSERT_GT(render_last_left(), 0.0F);

    // 同じ entity_id のバスとセンドを残したままバスを足しても、ミュート解除は引き継ぐ。
    config.buses.push_back(cp::RoutingBus{"bus-2", "B"});
    ASSERT_TRUE(engine_.apply_routing(config, false, {}));
    EXPECT_GT(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, RemovingStripMutesItsSendsBeforeRoutingIsReapplied) {
    add_audible_strip("ch-1");
    ASSERT_TRUE(engine_.apply_routing(prefader_send_through_bus(), false, {}));
    for (const char* id : {"send-1", "send-2", "bus-1"}) {
        engine_.set_route_muted(id, false);
    }

    // session が参照を外した構成を適用し直す前（または適用し直しに失敗したとき）でも、
    // 削除したストリップを送り元にするセンドはグラフから外れてミュートされ、警告が残る。
    engine_.remove_channel("ch-1");
    EXPECT_EQ(render_last_left(), 0.0F);
    const auto entries = logger_.entries();
    EXPECT_TRUE(std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.level == common::LogLevel::kWarning;
    }));

    // 同じ ID のストリップが戻っても、外したセンドはミュートのまま（初期ミュート）。
    add_audible_strip("ch-1");
    engine_.set_channel_muted("ch-1", true);
    EXPECT_EQ(render_last_left(), 0.0F);
}

TEST_F(EngineMixerTest, RejectsRoutingWithLoopWithoutDelay) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1", "A"});
    config.buses.push_back(cp::RoutingBus{"bus-2", "B"});
    config.sends.push_back(cp::RoutingSend{"send-1", "bus-1", "bus-2", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-2", "bus-1", cp::SendTap::kPostFader});
    EXPECT_FALSE(engine_.apply_routing(config, true, {}));
}

TEST_F(EngineMixerTest, ReadMetersReportsStripsAndMasterSortedById) {
    add_audible_strip("ch-2");
    engine_.add_channel("ch-1");
    (void)render_last_left();

    const auto meters = engine_.read_meters();

    ASSERT_EQ(meters.size(), 3U);
    EXPECT_EQ(meters[0].entity_id, "ch-1");
    EXPECT_EQ(meters[1].entity_id, "ch-2");
    EXPECT_EQ(meters[2].entity_id, "master");
    EXPECT_FALSE(meters[0].peak_db.has_value());
    ASSERT_TRUE(meters[1].peak_db.has_value());
    EXPECT_NEAR(*meters[1].peak_db, -20.0, 0.1);
    EXPECT_TRUE(meters[2].peak_db.has_value());
}

// --- 返送（Minus-Me） ---

TEST_F(EngineMixerTest, ReturnCarriesOtherStripsButNotTheOwnOne) {
    add_audible_strip("ch-1");
    add_audible_strip("ch-2");
    auto sink1 = std::make_shared<ac::FakeAudioSink>();
    auto sink2 = std::make_shared<ac::FakeAudioSink>();
    ASSERT_TRUE(engine_.attach_return(1, {"ch-1"}, sink1));
    ASSERT_TRUE(engine_.attach_return(2, {"ch-2"}, sink2));
    (void)render_last_left();
    // それぞれの返送には、相手のストリップだけが入る（自分の声は入らない）。
    EXPECT_NEAR(sink1->last(), kInput, 1e-5F);
    EXPECT_NEAR(sink2->last(), kInput, 1e-5F);

    // ch-2 をチャンネルミュートすると、ch-1 の返送からも消える（ポストフェーダー）。
    engine_.set_channel_muted("ch-2", true);
    (void)render_last_left();
    EXPECT_EQ(sink1->last(), 0.0F);
}

TEST_F(EngineMixerTest, ReturnIsIndependentOfTheMasterMute) {
    add_audible_strip("ch-1");
    add_audible_strip("ch-2");
    auto sink1 = std::make_shared<ac::FakeAudioSink>();
    ASSERT_TRUE(engine_.attach_return(1, {"ch-1"}, sink1));
    engine_.set_master_muted(true);
    // ハウスの出力を止めても、出演者のモニターは止めない。
    EXPECT_EQ(render_last_left(), 0.0F);
    EXPECT_NEAR(sink1->last(), kInput, 1e-5F);
}

TEST_F(EngineMixerTest, DetachReturnStopsFeedingTheSink) {
    add_audible_strip("ch-1");
    add_audible_strip("ch-2");
    auto sink1 = std::make_shared<ac::FakeAudioSink>();
    ASSERT_TRUE(engine_.attach_return(1, {"ch-1"}, sink1));
    (void)render_last_left();
    const auto received = sink1->received();
    engine_.detach_return(1);
    (void)render_last_left();
    EXPECT_EQ(sink1->received(), received);
}

TEST_F(EngineMixerTest, ReturnOfAClientWithSeveralInputsCarriesOnlyTheOthers) {
    // ch-1 と ch-2 は同じ子機の 2 つの入力、ch-3 は別の子機。
    add_audible_strip("ch-1");
    add_audible_strip("ch-2");
    add_audible_strip("ch-3");
    auto sink = std::make_shared<ac::FakeAudioSink>();
    ASSERT_TRUE(engine_.attach_return(1, {"ch-1", "ch-2"}, sink));
    (void)render_last_left();
    EXPECT_NEAR(sink->last(), kInput, 1e-5F);

    // 子機の入力が 1 本に減ったら、同じキーで自分のストリップの一覧を付け替える。
    ASSERT_TRUE(engine_.attach_return(1, {"ch-1"}, sink));
    (void)render_last_left();
    EXPECT_NEAR(sink->last(), 2.0F * kInput, 1e-5F);
}

TEST_F(EngineMixerTest, AttachReturnFailsForUnknownStripOrNullSink) {
    engine_.add_channel("ch-1");
    EXPECT_FALSE(engine_.attach_return(1, {"ch-unknown"}, std::make_shared<ac::FakeAudioSink>()));
    EXPECT_FALSE(engine_.attach_return(1, {}, std::make_shared<ac::FakeAudioSink>()));
    EXPECT_FALSE(engine_.attach_return(1, {"ch-1"}, nullptr));
}

}  // namespace
}  // namespace vocroz::engine

namespace vocroz::engine {
namespace {

TEST_F(EngineMixerTest, AttachedMidiSourceIsPulledEveryCallbackUntilDetached) {
    auto midi = std::make_shared<audio_core::FakeMidiSource>();
    ASSERT_TRUE(engine_.attach_midi_source("device:a", midi));
    // 経路がなくてもグラフが取り出すので、キューはあふれない。
    static_cast<void>(render_last_left());
    const auto pulled = midi->pull_count();
    EXPECT_GT(pulled, 0U);

    engine_.detach_midi_source("device:a");
    static_cast<void>(render_last_left());
    EXPECT_EQ(midi->pull_count(), pulled);
}

TEST_F(EngineMixerTest, AttachingTheSameKeyReplacesTheSource) {
    auto first = std::make_shared<audio_core::FakeMidiSource>();
    auto second = std::make_shared<audio_core::FakeMidiSource>();
    ASSERT_TRUE(engine_.attach_midi_source("strip:ch-1", first));
    ASSERT_TRUE(engine_.attach_midi_source("strip:ch-1", second));
    static_cast<void>(render_last_left());
    EXPECT_EQ(first->pull_count(), 0U);
    EXPECT_GT(second->pull_count(), 0U);
}

TEST_F(EngineMixerTest, AttachMidiSourceRejectsNull) {
    EXPECT_FALSE(engine_.attach_midi_source("device:a", nullptr));
}

TEST_F(EngineMixerTest, RemovingAStripNotifiesAndDetachesItsMidi) {
    std::vector<std::string> removed;
    engine_.set_strip_removal_handler([&](const std::string& id) { removed.push_back(id); });
    engine_.add_channel("ch-1");
    auto midi = std::make_shared<audio_core::FakeMidiSource>();
    ASSERT_TRUE(engine_.attach_midi_source(strip_midi_key("ch-1"), midi));

    engine_.remove_channel("ch-1");
    EXPECT_EQ(removed, (std::vector<std::string>{"ch-1"}));
    const auto pulled = midi->pull_count();
    static_cast<void>(render_last_left());
    EXPECT_EQ(midi->pull_count(), pulled);
}

}  // namespace
}  // namespace vocroz::engine
