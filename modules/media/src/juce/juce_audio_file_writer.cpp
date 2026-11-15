// SPDX-License-Identifier: AGPL-3.0-only
//
// JuceAudioFileWriter の実装。

#include "juce/juce_audio_file_writer.h"

#include <algorithm>
#include <array>
#include <utility>

#include "media_constants.h"

namespace vocroz::media {
namespace {

using audio_core::AudioFileError;
using common::make_unexpected;

[[nodiscard]] std::unique_ptr<juce::AudioFormat> make_format(audio_core::AudioFileFormat format) {
    switch (format) {
        case audio_core::AudioFileFormat::kWav:
            return std::make_unique<juce::WavAudioFormat>();
        case audio_core::AudioFileFormat::kFlac:
            return std::make_unique<juce::FlacAudioFormat>();
    }
    return std::make_unique<juce::WavAudioFormat>();
}

[[nodiscard]] int quality_of(audio_core::AudioFileFormat format) noexcept {
    switch (format) {
        case audio_core::AudioFileFormat::kWav:
            return 0;
        case audio_core::AudioFileFormat::kFlac:
            return kFlacQualityOptionIndex;
    }
    return 0;
}

}  // namespace

common::Result<std::unique_ptr<JuceAudioFileWriter>, AudioFileError> JuceAudioFileWriter::open(
    const juce::File& file, audio_core::AudioFileFormat format, std::size_t channels,
    detail::FreeSpaceGuard guard) {
    if (channels == 0 || channels > kMaxFileChannels) {
        return make_unexpected(AudioFileError::kUnsupportedFormat);
    }
    // 録ったファイルを黙って上書きしない。テイクのフォルダは開始ごとに新しく作るので、
    // 同じ名前があるのは、呼び出し側の誤りか、別のアプリが作ったファイル。
    if (file.exists()) {
        return make_unexpected(AudioFileError::kIoError);
    }
    if (guard.is_below_minimum()) {
        return make_unexpected(AudioFileError::kDiskFull);
    }
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (stream->failedToOpen()) {
        return make_unexpected(AudioFileError::kIoError);
    }
    juce::FileOutputStream& stream_ref = *stream;
    std::unique_ptr<juce::OutputStream> output = std::move(stream);
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(static_cast<double>(audio_core::kSampleRate))
                             .withNumChannels(static_cast<int>(channels))
                             .withBitsPerSample(kRecordingBitsPerSample)
                             .withQualityOptionIndex(quality_of(format));
    auto writer = make_format(format)->createWriterFor(output, options);
    if (!writer) {
        // 作れなかったときは、ストリームは output に残っている。空のファイルを残さない。
        output.reset();
        (void)file.deleteFile();
        return make_unexpected(AudioFileError::kIoError);
    }
    return std::unique_ptr<JuceAudioFileWriter>(
        new JuceAudioFileWriter(std::move(writer), stream_ref, channels, std::move(guard)));
}

JuceAudioFileWriter::JuceAudioFileWriter(std::unique_ptr<juce::AudioFormatWriter> writer,
                                         juce::FileOutputStream& stream, std::size_t channels,
                                         detail::FreeSpaceGuard guard)
    : writer_(std::move(writer)),
      stream_(&stream),
      channels_(channels),
      guard_(std::move(guard)),
      planar_(channels, std::vector<float>(kReadChunkFrames, 0.0F)) {}

JuceAudioFileWriter::~JuceAudioFileWriter() = default;

AudioFileError JuceAudioFileWriter::fail() {
    const AudioFileError error =
        guard_.is_below_minimum() ? AudioFileError::kDiskFull : AudioFileError::kIoError;
    failure_ = error;
    return error;
}

common::Result<void, AudioFileError> JuceAudioFileWriter::write(
    std::span<const float> interleaved) {
    if (failure_) {
        return make_unexpected(*failure_);
    }
    if (!writer_) {
        return make_unexpected(AudioFileError::kIoError);
    }
    std::size_t frames = interleaved.size() / channels_;
    if (!guard_.admit(static_cast<std::uint64_t>(frames) * channels_ * kBytesPerRecordedSample)) {
        failure_ = AudioFileError::kDiskFull;
        return make_unexpected(AudioFileError::kDiskFull);
    }
    std::size_t offset = 0;
    while (frames > 0) {
        const std::size_t chunk = std::min(frames, kReadChunkFrames);
        std::array<const float*, kMaxFileChannels> pointers{};
        for (std::size_t ch = 0; ch < channels_; ++ch) {
            for (std::size_t i = 0; i < chunk; ++i) {
                planar_[ch][i] = interleaved[(offset + i) * channels_ + ch];
            }
            pointers[ch] = planar_[ch].data();
        }
        if (!writer_->writeFromFloatArrays(pointers.data(), static_cast<int>(channels_),
                                           static_cast<int>(chunk))) {
            return make_unexpected(fail());
        }
        offset += chunk;
        frames -= chunk;
        frames_since_flush_ += chunk;
    }
    if (frames_since_flush_ >= kHeaderFlushFrames) {
        frames_since_flush_ = 0;
        // WAV はヘッダーを書き直してから続きの位置へ戻る。FLAC の書き出しは flush に対応せず
        // false を返すが、FLAC のフレームはそれぞれ独立して読めるので、異常終了しても
        // 途中までを読める。どちらもストリームの状態で書き込みの失敗を確かめる。
        (void)writer_->flush();
        stream_->flush();
    }
    if (stream_->getStatus().failed()) {
        return make_unexpected(fail());
    }
    return {};
}

common::Result<void, AudioFileError> JuceAudioFileWriter::finish() {
    if (!writer_) {
        return failure_ ? common::Result<void, AudioFileError>(make_unexpected(*failure_))
                        : common::Result<void, AudioFileError>();
    }
    // 失敗の後でも、書けたところまでを再生できるファイルにして閉じる（ADR-0031）。
    (void)writer_->flush();
    stream_->flush();
    const bool failed = stream_->getStatus().failed();
    // 破棄でヘッダーを確定してストリームを閉じる。
    writer_.reset();
    stream_ = nullptr;
    if (failed && !failure_) {
        (void)fail();
    }
    if (failure_) {
        return make_unexpected(*failure_);
    }
    return {};
}

}  // namespace vocroz::media
