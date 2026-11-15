// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE による録音の書き出しと再生の読み込みのテスト。実際のファイルを一時フォルダに書いて読み戻し、
// 形式ごとの往復、上書きの禁止、容量不足での停止、書き出し中のヘッダーの書き直し、
// 48 kHz 以外のファイルの変換とシークを確かめる。

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "vocroz/media/factories.h"

#include "detail/free_space_guard.h"
#include "juce/juce_audio_file_writer.h"
#include "media_constants.h"

namespace vocroz::media {
namespace {

namespace fs = std::filesystem;
using audio_core::AudioFileError;
using audio_core::AudioFileFormat;

constexpr std::size_t kRate = audio_core::kSampleRate;
// 24 bit の量子化の誤差（1 LSB は約 1.2e-7）に余裕を持たせた許容差。
constexpr float kTolerance = 1e-5F;

// テストごとの一時フォルダ。終わったら中身ごと消す。
class TempFolder {
public:
    TempFolder() {
        std::random_device random;
        path_ = fs::temp_directory_path() / ("vocroz-media-test-" + std::to_string(random()));
        fs::create_directories(path_);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

// フレーム f、チャンネル ch の試験信号。位置ごとに違う値で、24 bit で表せる範囲に収める。
[[nodiscard]] float pattern(std::size_t frame, std::size_t ch) {
    return static_cast<float>((frame % 2000)) / 4000.0F - 0.25F + static_cast<float>(ch) * 0.5F;
}

[[nodiscard]] std::vector<float> make_pattern(std::size_t frames, std::size_t channels) {
    std::vector<float> samples(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            samples[i * channels + ch] = pattern(i, ch);
        }
    }
    return samples;
}

[[nodiscard]] std::vector<float> read_all(audio_core::AudioFileReader& reader) {
    std::vector<float> all;
    std::vector<float> chunk(kReadChunkFrames * reader.channel_count());
    while (true) {
        const auto frames = reader.read(chunk);
        EXPECT_TRUE(frames.has_value());
        if (!frames || frames.value() == 0) {
            break;
        }
        all.insert(
            all.end(), chunk.begin(),
            chunk.begin() + static_cast<std::ptrdiff_t>(frames.value() * reader.channel_count()));
    }
    return all;
}

[[nodiscard]] juce::File juce_file(const fs::path& path) {
    return juce::File(juce::String(path.string()));
}

void expect_round_trip(AudioFileFormat format, std::size_t channels, const std::string& name) {
    TempFolder folder;
    const fs::path file = folder.path() / name;
    constexpr std::size_t kFrames = kRate + 123;
    const auto samples = make_pattern(kFrames, channels);
    {
        auto writer = make_audio_file_writer(file, format, channels);
        ASSERT_TRUE(writer.has_value());
        // ディスク I/O スレッドと同じく、小分けにして書く。
        constexpr std::size_t kChunk = 1000;
        for (std::size_t offset = 0; offset < kFrames; offset += kChunk) {
            const std::size_t frames = std::min(kChunk, kFrames - offset);
            ASSERT_TRUE(writer.value()
                            ->write(std::span<const float>(samples).subspan(offset * channels,
                                                                            frames * channels))
                            .has_value());
        }
        ASSERT_TRUE(writer.value()->finish().has_value());
    }
    auto reader = make_audio_file_reader(file);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader.value()->channel_count(), channels);
    EXPECT_EQ(reader.value()->length_frames(), kFrames);
    const auto read = read_all(*reader.value());
    ASSERT_EQ(read.size(), samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        ASSERT_NEAR(read[i], samples[i], kTolerance) << "sample " << i;
    }
}

TEST(JuceAudioFileTest, WavStereoRoundTripsAt24Bit) {
    expect_round_trip(AudioFileFormat::kWav, 2, "stereo.wav");
}

TEST(JuceAudioFileTest, FlacMonoRoundTripsAt24Bit) {
    expect_round_trip(AudioFileFormat::kFlac, 1, "mono.flac");
}

TEST(JuceAudioFileTest, FlacStereoRoundTripsAt24Bit) {
    expect_round_trip(AudioFileFormat::kFlac, 2, "stereo.flac");
}

TEST(JuceAudioFileTest, WriterDoesNotOverwriteExistingFile) {
    TempFolder folder;
    const fs::path file = folder.path() / "take.wav";
    std::ofstream(file) << "keep";
    const auto writer = make_audio_file_writer(file, AudioFileFormat::kWav, 1);
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), AudioFileError::kIoError);
    std::ifstream in(file);
    std::string text;
    in >> text;
    EXPECT_EQ(text, "keep");
}

TEST(JuceAudioFileTest, WriterRejectsUnsupportedChannelCount) {
    TempFolder folder;
    const auto writer = make_audio_file_writer(folder.path() / "a.wav", AudioFileFormat::kWav, 3);
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), AudioFileError::kUnsupportedFormat);
}

TEST(JuceAudioFileTest, WriterFailsWhenFolderIsMissing) {
    TempFolder folder;
    const auto writer =
        make_audio_file_writer(folder.path() / "missing" / "a.wav", AudioFileFormat::kWav, 1);
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), AudioFileError::kIoError);
}

TEST(JuceAudioFileTest, StopsWritingBeforeVolumeRunsOutAndKeepsWrittenAudio) {
    TempFolder folder;
    const fs::path file = folder.path() / "full.wav";
    // 空きを調べる間隔を 1 バイトにして、書き出しのたびに空きを見る。
    std::uint64_t free_bytes = kMinFreeDiskBytes * 2;
    detail::FreeSpaceGuard guard(
        [&free_bytes]() { return std::optional<std::uint64_t>(free_bytes); }, kMinFreeDiskBytes, 1);
    auto writer =
        JuceAudioFileWriter::open(juce_file(file), AudioFileFormat::kWav, 1, std::move(guard));
    ASSERT_TRUE(writer.has_value());
    const auto samples = make_pattern(kRate, 1);
    ASSERT_TRUE(writer.value()->write(samples).has_value());

    // 空きが下限を下回ったら、容量不足を返して以降は書かない。
    free_bytes = kMinFreeDiskBytes - 1;
    const auto refused = writer.value()->write(samples);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), AudioFileError::kDiskFull);
    free_bytes = kMinFreeDiskBytes * 2;
    const auto still_refused = writer.value()->write(samples);
    ASSERT_FALSE(still_refused.has_value());
    EXPECT_EQ(still_refused.error(), AudioFileError::kDiskFull);

    // 閉じると容量不足を返すが、書けたぶんは再生できるファイルとして残る。
    const auto finished = writer.value()->finish();
    ASSERT_FALSE(finished.has_value());
    EXPECT_EQ(finished.error(), AudioFileError::kDiskFull);
    auto reader = make_audio_file_reader(file);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader.value()->length_frames(), kRate);
}

TEST(JuceAudioFileTest, WriterRefusesToOpenWhenVolumeIsAlreadyFull) {
    TempFolder folder;
    detail::FreeSpaceGuard guard([] { return std::optional<std::uint64_t>(0); }, kMinFreeDiskBytes,
                                 kFreeSpaceCheckBytes);
    const auto writer = JuceAudioFileWriter::open(juce_file(folder.path() / "a.wav"),
                                                  AudioFileFormat::kWav, 1, std::move(guard));
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), AudioFileError::kDiskFull);
}

TEST(JuceAudioFileTest, WavHeaderIsRewrittenWhileRecording) {
    TempFolder folder;
    const fs::path file = folder.path() / "crash.wav";
    auto writer = make_audio_file_writer(file, AudioFileFormat::kWav, 2);
    ASSERT_TRUE(writer.has_value());
    // 1.5 秒書いたところで（閉じる前に）エンジンが落ちたとしても、1 秒の時点のヘッダーで読める。
    const auto samples = make_pattern(kRate / 2, 2);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(writer.value()->write(samples).has_value());
    }
    auto reader = make_audio_file_reader(file);
    ASSERT_TRUE(reader.has_value());
    EXPECT_GE(reader.value()->length_frames(), kRate);
    ASSERT_TRUE(writer.value()->finish().has_value());
}

TEST(JuceAudioFileTest, ReaderReportsMissingAndUnsupportedFiles) {
    TempFolder folder;
    const auto missing = make_audio_file_reader(folder.path() / "none.wav");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), AudioFileError::kNotFound);

    const fs::path text = folder.path() / "notes.wav";
    std::ofstream(text) << "this is not audio";
    const auto unsupported = make_audio_file_reader(text);
    ASSERT_FALSE(unsupported.has_value());
    EXPECT_EQ(unsupported.error(), AudioFileError::kUnsupportedFormat);
}

// 44.1 kHz のモノラルの正弦波のファイルを JUCE で直接書く。
void write_sine_44k(const fs::path& path, std::size_t samples, double frequency) {
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::FileOutputStream>(juce_file(path));
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(44'100.0)
                                                  .withNumChannels(1)
                                                  .withBitsPerSample(24));
    ASSERT_NE(writer, nullptr);
    juce::AudioBuffer<float> buffer(1, static_cast<int>(samples));
    for (std::size_t i = 0; i < samples; ++i) {
        buffer.setSample(0, static_cast<int>(i),
                         static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * frequency *
                                                           static_cast<double>(i) / 44'100.0)));
    }
    ASSERT_TRUE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

TEST(JuceAudioFileTest, ReaderConvertsOtherSampleRatesTo48k) {
    TempFolder folder;
    const fs::path file = folder.path() / "44k.wav";
    write_sine_44k(file, 44'100, 1000.0);
    auto reader = make_audio_file_reader(file);
    ASSERT_TRUE(reader.has_value());
    // 1 秒のファイルは、48 kHz で 48000 フレームになる。
    EXPECT_EQ(reader.value()->length_frames(), kRate);
    const auto read = read_all(*reader.value());
    ASSERT_EQ(read.size(), kRate);
    // 変換の立ち上がりを除いた区間で、振幅（0.5）と周波数（1 kHz は 48 サンプル周期）が保たれる。
    float peak = 0.0F;
    for (std::size_t i = kRate / 4; i < kRate * 3 / 4; ++i) {
        peak = std::max(peak, std::abs(read[i]));
        ASSERT_NEAR(read[i], read[i + 48], 0.02F) << "sample " << i;
    }
    EXPECT_NEAR(peak, 0.5F, 0.02F);
}

TEST(JuceAudioFileTest, SeekMovesTheReadPosition) {
    TempFolder folder;
    const fs::path file = folder.path() / "seek.wav";
    {
        auto writer = make_audio_file_writer(file, AudioFileFormat::kWav, 1);
        ASSERT_TRUE(writer.has_value());
        ASSERT_TRUE(writer.value()->write(make_pattern(kRate, 1)).has_value());
        ASSERT_TRUE(writer.value()->finish().has_value());
    }
    auto reader = make_audio_file_reader(file);
    ASSERT_TRUE(reader.has_value());
    ASSERT_TRUE(reader.value()->seek(kRate / 2 + 7).has_value());
    std::vector<float> chunk(16);
    const auto frames = reader.value()->read(chunk);
    ASSERT_TRUE(frames.has_value());
    ASSERT_EQ(frames.value(), 16U);
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_NEAR(chunk[i], pattern(kRate / 2 + 7 + i, 0), kTolerance);
    }
    // 長さを超える位置はファイルの終わりとして扱う。
    ASSERT_TRUE(reader.value()->seek(kRate * 10).has_value());
    const auto at_end = reader.value()->read(chunk);
    ASSERT_TRUE(at_end.has_value());
    EXPECT_EQ(at_end.value(), 0U);
}

TEST(JuceAudioFileTest, ReaderUsesFirstTwoChannelsOfMultichannelFiles) {
    TempFolder folder;
    const fs::path file = folder.path() / "quad.wav";
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream =
            std::make_unique<juce::FileOutputStream>(juce_file(file));
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate(48'000.0)
                                                      .withNumChannels(4)
                                                      .withBitsPerSample(24));
        ASSERT_NE(writer, nullptr);
        juce::AudioBuffer<float> buffer(4, 100);
        for (int ch = 0; ch < 4; ++ch) {
            for (int i = 0; i < 100; ++i) {
                buffer.setSample(ch, i, 0.1F * static_cast<float>(ch + 1));
            }
        }
        ASSERT_TRUE(writer->writeFromAudioSampleBuffer(buffer, 0, 100));
    }
    auto reader = make_audio_file_reader(file);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader.value()->channel_count(), 2U);
    const auto read = read_all(*reader.value());
    ASSERT_EQ(read.size(), 200U);
    EXPECT_NEAR(read[0], 0.1F, kTolerance);
    EXPECT_NEAR(read[1], 0.2F, kTolerance);
}

}  // namespace
}  // namespace vocroz::media
