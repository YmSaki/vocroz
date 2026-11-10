// SPDX-License-Identifier: AGPL-3.0-only
//
// MediaStreamEncoder の単体テスト。初期ミュートで送らないこと、解除するとフレーム長ごとに送ること、
// ミュートするとランプの後で送信が止まること、止めている間もタイムスタンプが進むこと、
// 世代番号を載せること、送信待ちの満杯で捨てたフレームを数えることを確かめる。
// SRTP 外部出力のためのステレオの入力（push_stereo()）と、チャンネル数のそろえ方も確かめる。

#include "vocroz/transport/media_stream_encoder.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/opus_frame_decoder.h"

#include "fakes/fake_media_frame_sink.h"

namespace vocroz::transport {
namespace {

// 5ms のフレーム。
constexpr std::size_t kFrame = 240;
// デバイスのコールバック長（フレーム長とそろえない）。
constexpr std::size_t kBlock = 128;

class MediaStreamEncoderTest : public ::testing::Test {
protected:
    MediaStreamEncoderTest() : encoder_(make_encoder(), sink_) {}

    static OpusFrameEncoder make_encoder() {
        auto created = OpusFrameEncoder::create({});
        EXPECT_TRUE(created);
        return std::move(created).value();
    }

    // samples サンプルぶんを kBlock ずつ渡す。
    void feed(std::size_t samples, float value = 0.25F) {
        const std::vector<float> block(kBlock, value);
        while (samples > 0) {
            const std::size_t n = std::min(samples, kBlock);
            encoder_.push(std::span<const float>(block).first(n));
            samples -= n;
        }
    }

    void unmute(std::uint32_t epoch) {
        ASSERT_TRUE(encoder_.set_params({false, epoch, 1.0F}));
    }

    void mute(std::uint32_t epoch) {
        ASSERT_TRUE(encoder_.set_params({true, epoch, 1.0F}));
    }

    FakeMediaFrameSink sink_;
    MediaStreamEncoder encoder_;
};

TEST_F(MediaStreamEncoderTest, SendsNothingWhileInitiallyMuted) {
    feed(kFrame * 10);
    EXPECT_TRUE(sink_.frames().empty());
    EXPECT_EQ(encoder_.dropped_frames(), 0U);
}

TEST_F(MediaStreamEncoderTest, SendsOneFramePerFrameLengthAfterUnmute) {
    unmute(1);
    feed(kFrame * 4);
    const auto frames = sink_.frames();
    ASSERT_EQ(frames.size(), 4U);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        EXPECT_EQ(frames[i].timestamp, i * kFrame);
        EXPECT_EQ(frames[i].mute_epoch, 1U);
        EXPECT_GT(frames[i].payload_size, 0U);
    }
}

TEST_F(MediaStreamEncoderTest, FramesDecodeAsOpus) {
    unmute(1);
    feed(kFrame);
    ASSERT_EQ(sink_.frames().size(), 1U);
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    std::vector<float> pcm(kFrame);
    EXPECT_TRUE(decoder.value().decode(sink_.frames()[0].payload_bytes(), pcm));
}

TEST_F(MediaStreamEncoderTest, StopsSendingAfterTheMuteRamp) {
    unmute(1);
    feed(kFrame * 2);
    mute(1);
    feed(kFrame * 10);
    // ランプ（5ms）が終わるまでのフレームだけを送り、以後は送らない。
    const std::size_t sent = sink_.frames().size();
    EXPECT_LE(sent, 2U + kStreamMuteRampSamples / kFrame + 1U);
    feed(kFrame * 10);
    EXPECT_EQ(sink_.frames().size(), sent);
}

TEST_F(MediaStreamEncoderTest, TimestampsKeepAdvancingWhileMuted) {
    feed(kFrame * 3);
    unmute(1);
    feed(kFrame);
    ASSERT_EQ(sink_.frames().size(), 1U);
    // ミュート中の 3 フレームぶんも実時間どおりに進んでいる。
    EXPECT_EQ(sink_.frames()[0].timestamp, 3 * kFrame);
}

TEST_F(MediaStreamEncoderTest, FramesAfterResumeCarryTheNewEpoch) {
    unmute(1);
    feed(kFrame);
    mute(1);
    feed(kFrame * 4);
    sink_.clear();
    unmute(2);
    feed(kFrame * 2);
    ASSERT_FALSE(sink_.frames().empty());
    for (const auto& frame : sink_.frames()) {
        EXPECT_EQ(frame.mute_epoch, 2U);
    }
}

TEST_F(MediaStreamEncoderTest, CountsFramesDroppedBecauseTheSinkIsFull) {
    unmute(1);
    sink_.set_full(true);
    feed(kFrame * 3);
    EXPECT_EQ(encoder_.dropped_frames(), 3U);
}

TEST_F(MediaStreamEncoderTest, FadesInAfterUnmute) {
    // 解除の直後はランプで立ち上げ、クリックノイズを出さない。最初のフレームの先頭は小さい。
    unmute(1);
    feed(kFrame, 0.5F);
    ASSERT_EQ(sink_.frames().size(), 1U);
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    std::vector<float> pcm(kFrame);
    ASSERT_TRUE(decoder.value().decode(sink_.frames()[0].payload_bytes(), pcm));
    EXPECT_LT(std::abs(pcm[0]), 0.1F);
}

// 1 kHz の正弦波を frames サンプルぶん作る。
std::vector<float> sine(std::size_t frames, float amplitude) {
    constexpr double kPi = 3.14159265358979323846;
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i] = amplitude * static_cast<float>(
                                 std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / 48000.0));
    }
    return out;
}

double rms(std::span<const float> samples) {
    double sum = 0.0;
    for (const float s : samples) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

// 送ったフレームを順にデコードし、最後のフレームのインターリーブした PCM を返す。
std::vector<float> decode_last(const FakeMediaFrameSink& sink, std::size_t channels) {
    auto decoder = OpusFrameDecoder::create(channels);
    EXPECT_TRUE(decoder);
    std::vector<float> pcm(kFrame * channels);
    for (const auto& frame : sink.frames()) {
        EXPECT_TRUE(decoder.value().decode(frame.payload_bytes(), pcm));
    }
    return pcm;
}

class MediaStreamEncoderChannelsTest : public ::testing::Test {
protected:
    static OpusFrameEncoder make_encoder(std::size_t channels) {
        OpusEncoderConfig config;
        config.channels = channels;
        auto created = OpusFrameEncoder::create(config);
        EXPECT_TRUE(created);
        return std::move(created).value();
    }

    // ランプが終わったあとの定常状態を見るため、20 フレームぶんを kBlock ずつ渡す。
    static void feed_stereo(MediaStreamEncoder& encoder, const std::vector<float>& left,
                            const std::vector<float>& right) {
        for (std::size_t offset = 0; offset < left.size(); offset += kBlock) {
            const std::size_t n = std::min(kBlock, left.size() - offset);
            encoder.push_stereo(std::span<const float>(left).subspan(offset, n),
                                std::span<const float>(right).subspan(offset, n));
        }
    }

    static constexpr std::size_t kFrames = kFrame * 20;
    FakeMediaFrameSink sink_;
};

TEST_F(MediaStreamEncoderChannelsTest, StereoEncoderKeepsLeftAndRightApart) {
    MediaStreamEncoder encoder(make_encoder(2), sink_);
    ASSERT_TRUE(encoder.set_params({false, 1, 1.0F}));
    feed_stereo(encoder, sine(kFrames, 0.5F), std::vector<float>(kFrames, 0.0F));
    ASSERT_EQ(sink_.frames().size(), kFrames / kFrame);

    const auto pcm = decode_last(sink_, 2);
    std::vector<float> left;
    std::vector<float> right;
    for (std::size_t i = 0; i < kFrame; ++i) {
        left.push_back(pcm[i * 2]);
        right.push_back(pcm[i * 2 + 1]);
    }
    // L だけに音を入れたので、R はほぼ無音のまま（Opus の誤差を許す）。
    EXPECT_GT(rms(left), 0.2);
    EXPECT_LT(rms(right), rms(left) * 0.1);
}

TEST_F(MediaStreamEncoderChannelsTest, MonoEncoderAveragesLeftAndRight) {
    MediaStreamEncoder encoder(make_encoder(1), sink_);
    ASSERT_TRUE(encoder.set_params({false, 1, 1.0F}));
    // 逆相の L と R は平均で打ち消し合う。
    const auto left = sine(kFrames, 0.5F);
    std::vector<float> right(left.size());
    std::transform(left.begin(), left.end(), right.begin(), [](float v) { return -v; });
    feed_stereo(encoder, left, right);
    ASSERT_EQ(sink_.frames().size(), kFrames / kFrame);
    EXPECT_LT(rms(decode_last(sink_, 1)), 0.01);
}

TEST_F(MediaStreamEncoderChannelsTest, StereoEncoderDuplicatesMonoInput) {
    MediaStreamEncoder encoder(make_encoder(2), sink_);
    ASSERT_TRUE(encoder.set_params({false, 1, 1.0F}));
    const auto tone = sine(kFrames, 0.5F);
    for (std::size_t offset = 0; offset < tone.size(); offset += kBlock) {
        encoder.push(
            std::span<const float>(tone).subspan(offset, std::min(kBlock, kFrames - offset)));
    }
    const auto pcm = decode_last(sink_, 2);
    std::vector<float> left;
    std::vector<float> right;
    for (std::size_t i = 0; i < kFrame; ++i) {
        left.push_back(pcm[i * 2]);
        right.push_back(pcm[i * 2 + 1]);
    }
    EXPECT_GT(rms(left), 0.2);
    EXPECT_NEAR(rms(left), rms(right), 0.05);
}

}  // namespace
}  // namespace vocroz::transport
