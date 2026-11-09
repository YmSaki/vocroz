// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioFileReader。決まった長さの、位置から値がわかるサンプルを返す。
// フレーム f のチャンネル ch の値は sample_at(f, ch)。再生位置とシークの正しさを、出力の値から
// 確かめられるようにする。
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "vocroz/audio_core/ports/audio_file_reader.h"

namespace vocroz::audio_core {

/// 位置からサンプルの値が決まる AudioFileReader。
/// スレッド安全性: ディスク I/O スレッド（テストのスレッド）だけが呼ぶ。
class FakeAudioFileReader final : public AudioFileReader {
public:
    /// @param channel_count チャンネル数
    /// @param length_frames ファイルの長さ（フレーム）
    FakeAudioFileReader(std::size_t channel_count, std::uint64_t length_frames)
        : channel_count_(channel_count), length_(length_frames) {}

    /// フレーム frame のチャンネル ch の値を返す。0 にならず、位置ごとに違う値にする。
    [[nodiscard]] static float sample_at(std::uint64_t frame, std::size_t ch) noexcept {
        return static_cast<float>((frame % 1000) + 1) * 0.0001F + static_cast<float>(ch) * 0.5F;
    }

    /// 次の read() を error で失敗させる。
    void fail_next_read(AudioFileError error) noexcept {
        read_error_ = error;
    }

    /// 1 回の read() で返すフレーム数の上限を決める（デコードが遅いファイルの模擬）。
    void limit_frames_per_read(std::size_t frames) noexcept {
        max_frames_per_read_ = frames;
    }

    /// 現在の読み込み位置を返す。
    [[nodiscard]] std::uint64_t position() const noexcept {
        return position_;
    }

    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return channel_count_;
    }

    [[nodiscard]] common::Result<std::size_t, AudioFileError> read(
        std::span<float> interleaved) override {
        if (read_error_) {
            const AudioFileError error = *read_error_;
            read_error_.reset();
            return common::make_unexpected(error);
        }
        const std::uint64_t remaining = length_ - std::min(position_, length_);
        const std::size_t frames = static_cast<std::size_t>(std::min<std::uint64_t>(
            {remaining, interleaved.size() / channel_count_, max_frames_per_read_}));
        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t ch = 0; ch < channel_count_; ++ch) {
                interleaved[i * channel_count_ + ch] = sample_at(position_ + i, ch);
            }
        }
        position_ += frames;
        return frames;
    }

    [[nodiscard]] common::Result<void, AudioFileError> seek(std::uint64_t frame) override {
        position_ = std::min(frame, length_);
        return {};
    }

private:
    std::size_t channel_count_;
    std::uint64_t length_;
    std::uint64_t position_ = 0;
    std::uint64_t max_frames_per_read_ = std::numeric_limits<std::uint64_t>::max();
    std::optional<AudioFileError> read_error_;
};

}  // namespace vocroz::audio_core
