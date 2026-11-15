// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の AudioFormatWriter（WAV、FLAC）による AudioFileWriter の実装
// （docs/adr/0043-recording-and-media-player.md）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "vocroz/audio_core/audio_file_format.h"
#include "vocroz/audio_core/ports/audio_file_writer.h"

#include "detail/free_space_guard.h"

namespace vocroz::media {

/// 録音のファイル 1 つの書き出し。
/// スレッド安全性: 1 つのスレッド（ディスク I/O スレッド）からだけ使う。
class JuceAudioFileWriter final : public audio_core::AudioFileWriter {
public:
    /// ファイルを新しく作って開く。
    /// @param file     作るファイル。あれば kIoError（上書きしない）
    /// @param format   形式
    /// @param channels チャンネル数（1 か 2）
    /// @param guard    空き容量の判断（テストでは空きを返す関数を差し替える）
    /// @return 書き出し先か、失敗の理由
    [[nodiscard]] static common::Result<std::unique_ptr<JuceAudioFileWriter>,
                                        audio_core::AudioFileError>
    open(const juce::File& file, audio_core::AudioFileFormat format, std::size_t channels,
         detail::FreeSpaceGuard guard);

    JuceAudioFileWriter(const JuceAudioFileWriter&) = delete;
    JuceAudioFileWriter& operator=(const JuceAudioFileWriter&) = delete;
    ~JuceAudioFileWriter() override;

    [[nodiscard]] common::Result<void, audio_core::AudioFileError> write(
        std::span<const float> interleaved) override;
    [[nodiscard]] common::Result<void, audio_core::AudioFileError> finish() override;

private:
    JuceAudioFileWriter(std::unique_ptr<juce::AudioFormatWriter> writer,
                        juce::FileOutputStream& stream, std::size_t channels,
                        detail::FreeSpaceGuard guard);

    // 書き出しの失敗を、空き容量を調べて容量不足かそれ以外かに分けて記録する。
    [[nodiscard]] audio_core::AudioFileError fail();

    std::unique_ptr<juce::AudioFormatWriter> writer_;
    // writer_ が持つストリーム。書き込みの失敗をストリームの状態で確かめるのに使う。
    juce::FileOutputStream* stream_;
    std::size_t channels_;
    detail::FreeSpaceGuard guard_;
    // インターリーブ形式から JUCE の planar 形式へ並べ替える作業領域。
    std::vector<std::vector<float>> planar_;
    std::uint64_t frames_since_flush_ = 0;
    std::optional<audio_core::AudioFileError> failure_;
};

}  // namespace vocroz::media
