// SPDX-License-Identifier: AGPL-3.0-only
//
// JuceAudioFileReader の実装。

#include "juce/juce_audio_file_reader.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "media_constants.h"

namespace vocroz::media {
namespace {

using audio_core::AudioFileError;
using common::make_unexpected;

constexpr auto kEngineRate = static_cast<double>(audio_core::kSampleRate);

}  // namespace

common::Result<std::unique_ptr<JuceAudioFileReader>, AudioFileError> JuceAudioFileReader::open(
    const juce::File& file) {
    if (!file.existsAsFile()) {
        return make_unexpected(AudioFileError::kNotFound);
    }
    auto stream = file.createInputStream();
    if (!stream) {
        return make_unexpected(AudioFileError::kIoError);
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(std::unique_ptr<juce::InputStream>(std::move(stream))));
    if (!reader || reader->numChannels == 0 || reader->sampleRate <= 0.0 ||
        reader->lengthInSamples < 0) {
        return make_unexpected(AudioFileError::kUnsupportedFormat);
    }
    const auto channels = std::min<std::size_t>(reader->numChannels, kMaxFileChannels);
    return std::unique_ptr<JuceAudioFileReader>(
        new JuceAudioFileReader(std::move(reader), channels));
}

JuceAudioFileReader::JuceAudioFileReader(std::unique_ptr<juce::AudioFormatReader> reader,
                                         std::size_t channels)
    : file_rate_(reader->sampleRate),
      channels_(channels),
      // 48 kHz に換算した長さ。端数は切り捨てる（最後の 1 フレームに満たない部分は鳴らさない）。
      length_frames_(static_cast<std::uint64_t>(std::floor(
          static_cast<double>(reader->lengthInSamples) * kEngineRate / reader->sampleRate))),
      reader_source_(std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true)),
      buffer_(static_cast<int>(channels), static_cast<int>(kReadChunkFrames)) {
    if (file_rate_ != kEngineRate) {
        resampler_ = std::make_unique<juce::ResamplingAudioSource>(reader_source_.get(), false,
                                                                   static_cast<int>(channels));
        // 比は「出力 1 サンプルあたりに進む入力のサンプル数」。
        resampler_->setResamplingRatio(file_rate_ / kEngineRate);
        resampler_->prepareToPlay(static_cast<int>(kReadChunkFrames), kEngineRate);
    } else {
        reader_source_->prepareToPlay(static_cast<int>(kReadChunkFrames), kEngineRate);
    }
}

JuceAudioFileReader::~JuceAudioFileReader() = default;

juce::int64 JuceAudioFileReader::file_position_of(std::uint64_t frame) const noexcept {
    return static_cast<juce::int64>(
        std::llround(static_cast<double>(frame) * file_rate_ / kEngineRate));
}

common::Result<std::size_t, AudioFileError> JuceAudioFileReader::read(
    std::span<float> interleaved) {
    const std::uint64_t remaining = length_frames_ - std::min(position_, length_frames_);
    const auto frames = static_cast<std::size_t>(
        std::min<std::uint64_t>({remaining, interleaved.size() / channels_, kReadChunkFrames}));
    if (frames == 0) {
        return std::size_t{0};
    }
    const juce::AudioSourceChannelInfo info(&buffer_, 0, static_cast<int>(frames));
    if (resampler_) {
        resampler_->getNextAudioBlock(info);
    } else {
        reader_source_->getNextAudioBlock(info);
    }
    for (std::size_t ch = 0; ch < channels_; ++ch) {
        const float* source = buffer_.getReadPointer(static_cast<int>(ch));
        for (std::size_t i = 0; i < frames; ++i) {
            interleaved[i * channels_ + ch] = source[i];
        }
    }
    position_ += frames;
    return frames;
}

common::Result<void, AudioFileError> JuceAudioFileReader::seek(std::uint64_t frame) {
    position_ = std::min(frame, length_frames_);
    reader_source_->setNextReadPosition(file_position_of(position_));
    if (resampler_) {
        // 変換の途中の状態（前の位置の残り）を捨て、新しい位置の音だけを返す。
        resampler_->flushBuffers();
    }
    return {};
}

}  // namespace vocroz::media
