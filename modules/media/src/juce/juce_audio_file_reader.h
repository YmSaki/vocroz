// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の AudioFormatReader による AudioFileReader の実装。ファイルのサンプルレートが 48 kHz で
// なければ ResamplingAudioSource で変換する（docs/adr/0043-recording-and-media-player.md）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <juce_audio_formats/juce_audio_formats.h>

#include "vocroz/audio_core/ports/audio_file_reader.h"

namespace vocroz::media {

/// 再生するファイル 1 つの読み込み。
/// スレッド安全性: 1 つのスレッド（ディスク I/O スレッド）からだけ使う。channel_count() と
/// length_frames() はどのスレッドからも呼べる。
class JuceAudioFileReader final : public audio_core::AudioFileReader {
public:
    /// ファイルを開く。
    /// @param file 開くファイル
    /// @return 読み込み元か、失敗の理由（kNotFound、kUnsupportedFormat、kIoError）
    [[nodiscard]] static common::Result<std::unique_ptr<JuceAudioFileReader>,
                                        audio_core::AudioFileError>
    open(const juce::File& file);

    JuceAudioFileReader(const JuceAudioFileReader&) = delete;
    JuceAudioFileReader& operator=(const JuceAudioFileReader&) = delete;
    ~JuceAudioFileReader() override;

    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return channels_;
    }
    [[nodiscard]] std::uint64_t length_frames() const noexcept override {
        return length_frames_;
    }
    [[nodiscard]] common::Result<std::size_t, audio_core::AudioFileError> read(
        std::span<float> interleaved) override;
    [[nodiscard]] common::Result<void, audio_core::AudioFileError> seek(
        std::uint64_t frame) override;

private:
    JuceAudioFileReader(std::unique_ptr<juce::AudioFormatReader> reader, std::size_t channels);

    // 48 kHz の位置を、ファイルのサンプルの位置に換算する。
    [[nodiscard]] juce::int64 file_position_of(std::uint64_t frame) const noexcept;

    double file_rate_;
    std::size_t channels_;
    std::uint64_t length_frames_;
    std::uint64_t position_ = 0;
    // reader_source_ が reader を持ち、resampler_ が reader_source_
    // を参照する（宣言の逆順に破棄）。
    std::unique_ptr<juce::AudioFormatReaderSource> reader_source_;
    // ファイルのレートが 48 kHz なら null（変換しない）。
    std::unique_ptr<juce::ResamplingAudioSource> resampler_;
    juce::AudioBuffer<float> buffer_;
};

}  // namespace vocroz::media
