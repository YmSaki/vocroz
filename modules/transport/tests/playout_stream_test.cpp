// SPDX-License-Identifier: AGPL-3.0-only
//
// PlayoutStream の単体テスト。合成信号を Opus でエンコードして送り、受信側の出力を数値で確かめる。
// - 溜めている間は無音で、目標の深さに達したら音が出る
// -
// 欠損、順序逆転、重複、ジッターを与えても破綻せず、途絶が続けば無音になり、古い音を遅れて鳴らし続けない
// - ±100ppm のクロックドリフトがあっても、長時間にわたってバッファが溢れも枯渇もしない
// - ミュート解除後の新しい区間では、停止前のフレームを再生しない

#include "vocroz/transport/playout_stream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <numbers>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/opus_frame_encoder.h"

namespace vocroz::transport {
namespace {

constexpr std::size_t kFrame = 240;  // 5ms
constexpr std::size_t kBlock = 128;  // 内部の標準ブロックサイズ

// 1kHz の正弦波をエンコードしたフレームを作る送信側。
// エンコードは重いので、最初に数百フレームをエンコードしておき、ペイロードを使い回す。
class SineSender {
public:
    explicit SineSender(std::size_t distinct_frames = 200) {
        auto encoder = OpusFrameEncoder::create({});
        EXPECT_TRUE(encoder);
        std::vector<float> pcm(kFrame);
        std::array<std::uint8_t, kMaxOpusPayloadBytes> packet{};
        for (std::size_t f = 0; f < distinct_frames; ++f) {
            for (std::size_t i = 0; i < kFrame; ++i) {
                const double t = static_cast<double>(f * kFrame + i) / kSampleRate;
                pcm[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * t));
            }
            const auto bytes = encoder.value().encode(pcm, packet);
            EXPECT_TRUE(bytes);
            payloads_.emplace_back(packet.begin(),
                                   packet.begin() + static_cast<std::ptrdiff_t>(bytes.value()));
        }
    }

    // index 番目のフレーム（タイムスタンプ index * kFrame）を作る。
    [[nodiscard]] MediaFrame frame(std::uint64_t index, std::uint32_t epoch) const {
        const auto& payload = payloads_[index % payloads_.size()];
        auto frame = make_media_frame(index * kFrame, epoch, payload);
        EXPECT_TRUE(frame);
        return frame.value();
    }

private:
    std::vector<std::vector<std::uint8_t>> payloads_;
};

double rms(std::span<const float> samples) {
    double sum = 0.0;
    for (const float s : samples) {
        sum += static_cast<double>(s) * s;
    }
    return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

std::unique_ptr<PlayoutStream> make_stream(PlayoutConfig config = {}) {
    auto stream = PlayoutStream::create(config);
    EXPECT_TRUE(stream);
    return std::move(stream).value();
}

TEST(PlayoutStreamTest, RejectsInvalidConfig) {
    PlayoutConfig inverted;
    inverted.min_delay = std::chrono::milliseconds{30};
    inverted.max_delay = std::chrono::milliseconds{20};
    EXPECT_EQ(PlayoutStream::create(inverted).error(), PlayoutError::kInvalidConfig);

    PlayoutConfig too_short;
    too_short.min_delay = std::chrono::milliseconds{2};
    EXPECT_EQ(PlayoutStream::create(too_short).error(), PlayoutError::kInvalidConfig);

    PlayoutConfig too_long;
    too_long.max_delay = std::chrono::milliseconds{300};
    EXPECT_EQ(PlayoutStream::create(too_long).error(), PlayoutError::kInvalidConfig);
}

TEST(PlayoutStreamTest, OutputsSilenceUntilTargetDepthThenPlays) {
    const SineSender sender;
    auto stream = make_stream();
    std::vector<float> out(kBlock);

    stream->render(out);
    EXPECT_EQ(rms(out), 0.0);
    EXPECT_FALSE(stream->stats().playing);

    // 目標（既定の下限 10ms = 2 フレーム）に達するまでは再生しない。
    ASSERT_TRUE(stream->enqueue(sender.frame(0, 1)));
    stream->render(out);
    EXPECT_EQ(rms(out), 0.0);
    EXPECT_FALSE(stream->stats().playing);

    ASSERT_TRUE(stream->enqueue(sender.frame(1, 1)));
    std::uint64_t next = 2;
    double energy = 0.0;
    for (int i = 0; i < 200; ++i) {
        // 実時間で送られてくるフレームを模擬する（ブロック 128 に対してフレームは 240）。
        while (next * kFrame < static_cast<std::uint64_t>(i + 1) * kBlock + 2 * kFrame) {
            ASSERT_TRUE(stream->enqueue(sender.frame(next++, 1)));
        }
        stream->render(out);
        if (i > 100) {
            energy += rms(out);
        }
    }
    EXPECT_TRUE(stream->stats().playing);
    EXPECT_GT(energy / 99.0, 0.3);
    EXPECT_EQ(stream->stats().underruns, 0U);
}

TEST(PlayoutStreamTest, GoesSilentDuringOutageAndSkipsDelayedBacklog) {
    // 600ms
    // の途絶のあと、途絶中のフレームがまとめて届いても、それを順に鳴らして遅れたままにならない。
    const SineSender sender;
    auto stream = make_stream();
    std::vector<float> out(kBlock);
    std::uint64_t next = 0;
    std::uint64_t now = 0;  // 受信側の経過サンプル数
    auto deliver_until = [&](std::uint64_t until) {
        while (next * kFrame < until) {
            ASSERT_TRUE(stream->enqueue(sender.frame(next++, 1)));
        }
    };

    for (int i = 0; i < 400; ++i) {  // 約 1 秒の通常の受信
        deliver_until(now + 2 * kFrame);
        stream->render(out);
        now += kBlock;
    }
    ASSERT_TRUE(stream->stats().playing);

    // 途絶: 600ms の間フレームが届かない。送信側の時計は進み続ける。
    const std::uint64_t outage_end = now + kSampleRate * 6 / 10;
    double tail_energy = 0.0;
    while (now < outage_end) {
        stream->render(out);
        now += kBlock;
        // PLC の上限（40ms）とフェードアウトのあとは無音でなければならない。
        if (now > outage_end - kSampleRate * 5 / 10) {
            tail_energy += rms(out);
        }
    }
    EXPECT_EQ(tail_energy, 0.0);
    EXPECT_FALSE(stream->stats().playing);
    EXPECT_GE(stream->stats().rebuffers, 1U);

    // 途絶中のフレームがまとめて届く（ルーターのキューに溜まっていたなど）。
    deliver_until(now + 2 * kFrame);
    for (int i = 0; i < 200; ++i) {
        stream->render(out);
        now += kBlock;
        deliver_until(now + 2 * kFrame);
    }
    const PlayoutStats stats = stream->stats();
    EXPECT_TRUE(stats.playing);
    // 遅延は上限（既定 20ms）以内に戻っていて、途絶中の 600ms 分の音を遅れて再生し続けていない。
    EXPECT_LE(stats.depth_samples, 20.0 * kSampleRate / 1000.0 + 2 * kFrame);
    EXPECT_GT(stats.frames_dropped_overflow + stats.frames_late, 100U);
}

TEST(PlayoutStreamTest, SurvivesLossReorderDuplicationAndJitter) {
    // 欠損 5%、重複 3%、0〜15ms のジッター（順序逆転を含む）を 30 秒間与える。
    const SineSender sender;
    PlayoutConfig config;
    config.max_delay = std::chrono::milliseconds{40};
    auto stream = make_stream(config);
    std::mt19937 rng(2024);  // 決定的にするため種を固定する
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<std::uint64_t> jitter(0, 15 * kSampleRate / 1000);

    struct InFlight {
        std::uint64_t arrival;
        MediaFrame frame;
    };
    std::vector<InFlight> in_flight;
    std::vector<float> out(kBlock);
    std::uint64_t next = 0;
    std::uint64_t now = 0;
    float peak = 0.0F;
    for (int i = 0; i < 30 * 48000 / static_cast<int>(kBlock); ++i) {
        while (next * kFrame < now + kBlock) {
            const MediaFrame frame = sender.frame(next++, 1);
            if (unit(rng) < 0.05) {
                continue;
            }
            in_flight.push_back({frame.timestamp + jitter(rng), frame});
            if (unit(rng) < 0.03) {
                in_flight.push_back({frame.timestamp + jitter(rng), frame});
            }
        }
        // 到着時刻が来たものを、到着順（送信順ではない）に渡す。
        std::ranges::sort(in_flight, {}, &InFlight::arrival);
        auto ready =
            std::ranges::find_if(in_flight, [now](const InFlight& f) { return f.arrival > now; });
        for (auto it = in_flight.begin(); it != ready; ++it) {
            static_cast<void>(stream->enqueue(it->frame));
        }
        in_flight.erase(in_flight.begin(), ready);

        stream->render(out);
        now += kBlock;
        for (const float s : out) {
            ASSERT_TRUE(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
    }
    const PlayoutStats stats = stream->stats();
    EXPECT_TRUE(stats.playing);
    EXPECT_LT(peak, 1.0F);
    EXPECT_GT(stats.frames_duplicate, 0U);
    EXPECT_GT(stats.frames_concealed, 0U);
    // 欠損以外の大部分のフレームを再生できている。
    EXPECT_GT(static_cast<double>(stats.frames_decoded), 0.85 * static_cast<double>(next));
    // 目標はジッターに合わせて深くなるが、上限を超えない。
    EXPECT_GT(stats.target_delay_samples, 10U * kSampleRate / 1000);
    EXPECT_LE(stats.target_delay_samples, 40U * kSampleRate / 1000);
}

// 送信側の時計が ppm だけ速い（負なら遅い）状態で seconds 秒受信し、統計を返す。
// ジッターとして、0〜3ms の到着の遅れ（順序は保つ）を与える。
PlayoutStats run_with_clock_drift(double ppm, int seconds, PlayoutStats* after_warmup) {
    const SineSender sender;
    auto stream = make_stream();
    std::mt19937 rng(99);  // 決定的にするため種を固定する
    std::uniform_real_distribution<double> jitter_seconds(0.0, 0.003);
    std::vector<float> out(kBlock);
    const double sender_rate = kSampleRate * (1.0 + ppm * 1e-6);
    std::uint64_t next = 0;
    double next_arrival = 0.0;
    const int blocks = seconds * static_cast<int>(kSampleRate) / static_cast<int>(kBlock);
    const int warmup_blocks = 30 * static_cast<int>(kSampleRate) / static_cast<int>(kBlock);
    for (int b = 0; b < blocks; ++b) {
        const double now = static_cast<double>(b) * kBlock / kSampleRate;
        while (next_arrival <= now) {
            static_cast<void>(stream->enqueue(sender.frame(next, 1)));
            ++next;
            const double sent = static_cast<double>(next * kFrame) / sender_rate;
            next_arrival = std::max(next_arrival, sent + jitter_seconds(rng));
        }
        stream->render(out);
        if (b == warmup_blocks && after_warmup != nullptr) {
            *after_warmup = stream->stats();
        }
    }
    return stream->stats();
}

TEST(PlayoutStreamTest, CompensatesSenderClockFasterBy100ppm) {
    // 5 分間で 30ms
    // 分（目標の深さより大きい）のずれが溜まる条件で、溢れも枯渇もしないことを確かめる。
    PlayoutStats warm;
    const PlayoutStats end = run_with_clock_drift(100.0, 300, &warm);
    EXPECT_TRUE(end.playing);
    EXPECT_EQ(end.underruns, warm.underruns);
    EXPECT_EQ(end.frames_dropped_overflow, warm.frames_dropped_overflow);
    EXPECT_EQ(end.rebuffers, warm.rebuffers);
    EXPECT_NEAR(end.drift_estimate, 100e-6, 30e-6);
    EXPECT_LE(end.depth_samples, 20.0 * kSampleRate / 1000.0 + kFrame);
}

TEST(PlayoutStreamTest, CompensatesSenderClockSlowerBy100ppm) {
    PlayoutStats warm;
    const PlayoutStats end = run_with_clock_drift(-100.0, 300, &warm);
    EXPECT_TRUE(end.playing);
    EXPECT_EQ(end.underruns, warm.underruns);
    EXPECT_EQ(end.frames_dropped_overflow, warm.frames_dropped_overflow);
    EXPECT_EQ(end.rebuffers, warm.rebuffers);
    EXPECT_NEAR(end.drift_estimate, -100e-6, 30e-6);
    EXPECT_GT(end.depth_samples, 0.0);
}

TEST(PlayoutStreamTest, NewSegmentDiscardsFramesFromBeforeMute) {
    const SineSender sender;
    auto stream = make_stream();
    std::vector<float> out(kBlock);
    for (std::uint64_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(stream->enqueue(sender.frame(i, 1)));
    }
    stream->render(out);
    ASSERT_TRUE(stream->stats().playing);

    // ミュート（区間 1 の終わり）のあと、解除して区間 2 が始まった。
    // 区間 1 のフレームの残りと、区間 2 のフレームが前後して届く。
    ASSERT_TRUE(stream->enqueue(sender.frame(1000, 2)));
    ASSERT_TRUE(stream->enqueue(sender.frame(4, 1)));
    stream->render(out);
    PlayoutStats stats = stream->stats();
    EXPECT_EQ(stats.segments, 2U);
    EXPECT_EQ(stats.frames_stale_epoch, 1U);
    // 新しい区間は溜め直してから始めるので、まだ再生しない（停止前の音を鳴らさない）。
    EXPECT_FALSE(stats.playing);
    EXPECT_EQ(rms(out), 0.0);

    ASSERT_TRUE(stream->enqueue(sender.frame(1001, 2)));
    stream->render(out);
    stats = stream->stats();
    EXPECT_TRUE(stats.playing);
    // 再生の開始はフェードインするので、最初のサンプルは無音から立ち上がる。
    EXPECT_EQ(out[0], 0.0F);
}

TEST(PlayoutStreamTest, HandlesVariableCallbackLengths) {
    const SineSender sender;
    auto stream = make_stream();
    std::uint64_t next = 0;
    std::uint64_t now = 0;
    const std::array<std::size_t, 5> lengths{64, 512, 100, 256, 37};
    std::vector<float> out(512);
    for (int i = 0; i < 2000; ++i) {
        const std::size_t n = lengths[static_cast<std::size_t>(i) % lengths.size()];
        while (next * kFrame < now + n + 2 * kFrame) {
            ASSERT_TRUE(stream->enqueue(sender.frame(next++, 1)));
        }
        stream->render(std::span<float>{out}.first(n));
        now += n;
    }
    const PlayoutStats stats = stream->stats();
    EXPECT_TRUE(stats.playing);
    EXPECT_EQ(stats.rebuffers, 0U);
}

}  // namespace
}  // namespace vocroz::transport
