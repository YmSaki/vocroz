// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音タップと再生ノードを音声グラフに組み込んだときのテスト。Mixer を通して、
// 結線の検証、再生ノードからバスへの再生、録音タップへの書き込みを確かめる。
// ディスクの書き込みの遅延と容量不足は、別スレッドのディスク I/O と FakeAudioFileWriter で模擬し、
// その間もオーディオスレッドの処理が止まらず、ライブの出力が変わらないことを確かめる。

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/mixer.h"
#include "vocroz/audio_core/playback_track.h"
#include "vocroz/audio_core/recording_track.h"

#include "fakes/fake_audio_file_reader.h"
#include "fakes/fake_audio_file_writer.h"
#include "fakes/fake_audio_output.h"
#include "fakes/fake_audio_source.h"

namespace vocroz::audio_core {
namespace {

// フェーダーとフェードのランプ（10ms）が確実に終わる長さ。
constexpr std::size_t kAfterRamp = 1024;

class NullDeviceListener final : public AudioDeviceListener {
public:
    void on_device_lost() noexcept override {}
};

// ソース → ストリップ → Main バス → 出力と、ストリップのフェーダーの前から録音タップへの結線。
GraphDescription live_with_recording(std::shared_ptr<AudioSource> source,
                                     std::shared_ptr<RecordingTrack> track) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, std::move(source)),
                   NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0}),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, MeterId{1}),
                   NodeSpec::make_output(NodeId{4}, MeterId{2})};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{4}}};
    if (track) {
        graph.nodes.push_back(NodeSpec::make_record_tap(NodeId{5}, std::move(track)));
        graph.connections.push_back({NodeId{2}, NodeId{5}, SendTap::kPreFader});
    }
    return graph;
}

class RecordingAndPlaybackTest : public ::testing::Test {
protected:
    void start(const GraphDescription& graph) {
        ASSERT_TRUE(mixer_.apply_graph(graph).has_value());
        ASSERT_TRUE(mixer_.set_fader_muted(FaderId{0}, false).has_value());
        ASSERT_TRUE(mixer_.set_fader_muted(FaderId{1}, false).has_value());
        ASSERT_TRUE(output_.start(mixer_, listener_).has_value());
    }

    Mixer mixer_;
    NullDeviceListener listener_;
    FakeAudioOutput output_{2};
    std::shared_ptr<AudioSource> source_ =
        std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.25F));
};

TEST_F(RecordingAndPlaybackTest, RecordsPreFaderEvenWhileChannelIsMuted) {
    auto track = std::make_shared<RecordingTrack>(1);
    ASSERT_TRUE(mixer_.apply_graph(live_with_recording(source_, track)).has_value());
    ASSERT_TRUE(output_.start(mixer_, listener_).has_value());
    const std::vector<float> out = output_.render_frames(256);
    EXPECT_EQ(out[out.size() - 1], 0.0F);  // 初期ミュートなので聞こえない

    FakeAudioFileWriter writer(1);
    ASSERT_EQ(track->drain(writer).value(), 256U);
    for (const float sample : writer.samples()) {
        EXPECT_FLOAT_EQ(sample, 0.25F);  // フェーダーの前を録る
    }
}

TEST_F(RecordingAndPlaybackTest, RecordsStereoBusFromMonoAndStereoSources) {
    auto track = std::make_shared<RecordingTrack>(2);
    GraphDescription graph = live_with_recording(source_, nullptr);
    graph.nodes.push_back(NodeSpec::make_record_tap(NodeId{5}, track));
    graph.connections.push_back({NodeId{3}, NodeId{5}});  // Main バスのポストフェーダー
    start(graph);
    (void)output_.render_frames(kAfterRamp);
    FakeAudioFileWriter writer(2);
    ASSERT_TRUE(track->drain(writer).has_value());
    const std::vector<float> samples = writer.samples();
    ASSERT_EQ(samples.size(), 2 * kAfterRamp);
    EXPECT_FLOAT_EQ(samples[samples.size() - 2], 0.25F);
    EXPECT_FLOAT_EQ(samples[samples.size() - 1], 0.25F);
}

TEST_F(RecordingAndPlaybackTest, PlaysFileThroughBusToOutput) {
    auto track = std::make_shared<PlaybackTrack>(2, 4096);
    FakeAudioFileReader reader(2, kSampleRate);
    ASSERT_TRUE(track->fill(reader).has_value());
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_player(NodeId{1}, track),
                   NodeSpec::make_bus(NodeId{2}, FaderId{1}, std::nullopt),
                   NodeSpec::make_output(NodeId{3}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}};
    start(graph);

    (void)output_.render_frames(kAfterRamp);
    EXPECT_EQ(output_.render_frames(1)[0], 0.0F);  // play() まで鳴らない
    track->play();
    (void)output_.render_frames(kAfterRamp);
    const std::uint64_t position = track->position_frames();
    const std::vector<float> out = output_.render_frames(1);
    EXPECT_FLOAT_EQ(out[0], FakeAudioFileReader::sample_at(position, 0));
    EXPECT_FLOAT_EQ(out[1], FakeAudioFileReader::sample_at(position, 1));
}

TEST_F(RecordingAndPlaybackTest, RejectsInvalidTapAndPlayerConnections) {
    auto mono = std::make_shared<RecordingTrack>(1);
    auto player = std::make_shared<PlaybackTrack>(2, 128);

    GraphDescription stereo_into_mono = live_with_recording(source_, nullptr);
    stereo_into_mono.nodes.push_back(NodeSpec::make_record_tap(NodeId{5}, mono));
    stereo_into_mono.connections.push_back({NodeId{3}, NodeId{5}});
    EXPECT_EQ(mixer_.validate_graph(stereo_into_mono).error(), GraphError::kChannelMismatch);

    GraphDescription from_tap = live_with_recording(source_, mono);
    from_tap.connections.push_back({NodeId{5}, NodeId{3}});
    EXPECT_EQ(mixer_.validate_graph(from_tap).error(), GraphError::kInvalidConnection);

    GraphDescription into_player = live_with_recording(source_, nullptr);
    into_player.nodes.push_back(NodeSpec::make_player(NodeId{5}, player));
    into_player.connections.push_back({NodeId{3}, NodeId{5}});
    EXPECT_EQ(mixer_.validate_graph(into_player).error(), GraphError::kInvalidConnection);

    GraphDescription missing = live_with_recording(source_, nullptr);
    missing.nodes.push_back(NodeSpec::make_record_tap(NodeId{5}, nullptr));
    EXPECT_EQ(mixer_.validate_graph(missing).error(), GraphError::kMissingTrack);
    missing.nodes.back() = NodeSpec::make_player(NodeId{5}, nullptr);
    EXPECT_EQ(mixer_.validate_graph(missing).error(), GraphError::kMissingTrack);

    // 録音タップ以外へのプリフェーダーのセンドには、これまでどおりセンドレベルが要る。
    GraphDescription unleveled = live_with_recording(source_, nullptr);
    unleveled.connections.push_back({NodeId{2}, NodeId{3}, SendTap::kPreFader});
    unleveled.connections.erase(unleveled.connections.begin() + 1);
    EXPECT_EQ(mixer_.validate_graph(unleveled).error(), GraphError::kMissingSendLevel);
}

TEST_F(RecordingAndPlaybackTest, StalledDiskDoesNotStopAudioThreadOrLiveOutput) {
    // ディスク I/O スレッドが write() の中で止まっている間も、オーディオスレッドは処理を続け、
    // ライブの出力は変わらない。リングバッファ（ここでは 0.1 秒）を超えたぶんは欠落として数え、
    // 書き込みが戻ったら無音で埋めるので、ファイルの長さは処理したフレーム数と一致する。
    auto track = std::make_shared<RecordingTrack>(1, kSampleRate / 10);
    start(live_with_recording(source_, track));
    FakeAudioFileWriter writer(1);
    writer.stall();

    std::atomic<bool> running{true};
    std::thread disk([&] {
        while (running.load()) {
            (void)track->drain(writer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        (void)track->drain(writer);
    });

    // オーディオスレッドの役: 約 0.5 秒ぶんを処理する。止まった write() を待てば、ここで止まる。
    constexpr std::size_t kTotalFrames = 200 * kBlockSize;  // 約 0.53 秒
    std::vector<float> last;
    for (std::size_t done = 0; done < kTotalFrames; done += kBlockSize) {
        last = output_.render_frames(kBlockSize);
    }
    EXPECT_TRUE(writer.is_blocked() || writer.frames() == 0);
    EXPECT_FLOAT_EQ(last[last.size() - 2], 0.25F);  // ライブの出力はそのまま
    EXPECT_GT(track->dropped_frames(), 0U);

    writer.release();
    running.store(false);
    disk.join();
    // 録音を止める: タップを外したグラフを適用し、古いグラフが破棄されてから閉じる。
    ASSERT_TRUE(mixer_.apply_graph(live_with_recording(source_, nullptr)).has_value());
    (void)output_.render_frames(kBlockSize);
    mixer_.collect_garbage();
    ASSERT_TRUE(mixer_.is_graph_settled());
    ASSERT_TRUE(track->finish(writer).has_value());
    EXPECT_TRUE(writer.finished());
    EXPECT_EQ(writer.frames(), kTotalFrames);
    EXPECT_FALSE(track->failure().has_value());
}

TEST_F(RecordingAndPlaybackTest, DiskFullStopsRecordingButNotLiveOutput) {
    auto track = std::make_shared<RecordingTrack>(1);
    start(live_with_recording(source_, track));
    FakeAudioFileWriter writer(1);
    writer.set_capacity_frames(kSampleRate / 10);

    std::atomic<bool> running{true};
    std::thread disk([&] {
        while (running.load()) {
            (void)track->drain(writer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::vector<float> last;
    for (std::size_t done = 0; done < kSampleRate / 2; done += kBlockSize) {
        last = output_.render_frames(kBlockSize);
    }
    running.store(false);
    disk.join();
    // 容量不足になるまで書けたぶんを残し、以降は書かない。ライブの出力は止まらない。
    EXPECT_EQ(track->drain(writer).error(), AudioFileError::kDiskFull);
    EXPECT_EQ(track->failure(), AudioFileError::kDiskFull);
    EXPECT_LE(writer.frames(), kSampleRate / 10);
    // 止めるときは、書けたところまでを残してファイルを閉じる。
    EXPECT_EQ(track->finish(writer).error(), AudioFileError::kDiskFull);
    EXPECT_TRUE(writer.finished());
    EXPECT_FLOAT_EQ(last[last.size() - 2], 0.25F);
}

TEST_F(RecordingAndPlaybackTest, StartsAndStopsAllTapsOnSameBlock) {
    // 2 本のトラックのタップを 1 つのグラフで足し外しすると、どちらも同じ長さになる。
    auto first = std::make_shared<RecordingTrack>(1);
    auto second = std::make_shared<RecordingTrack>(2);
    GraphDescription graph = live_with_recording(source_, first);
    graph.nodes.push_back(NodeSpec::make_record_tap(NodeId{6}, second));
    graph.connections.push_back({NodeId{3}, NodeId{6}});
    start(live_with_recording(source_, nullptr));
    (void)output_.render_frames(300);
    ASSERT_TRUE(mixer_.apply_graph(graph).has_value());
    (void)output_.render_frames(1000);
    ASSERT_TRUE(mixer_.apply_graph(live_with_recording(source_, nullptr)).has_value());
    (void)output_.render_frames(500);
    mixer_.collect_garbage();
    EXPECT_TRUE(mixer_.is_graph_settled());

    FakeAudioFileWriter first_writer(1);
    FakeAudioFileWriter second_writer(2);
    ASSERT_TRUE(first->finish(first_writer).has_value());
    ASSERT_TRUE(second->finish(second_writer).has_value());
    EXPECT_EQ(first_writer.frames(), second_writer.frames());
    EXPECT_GT(first_writer.frames(), 0U);
}

}  // namespace
}  // namespace vocroz::audio_core
