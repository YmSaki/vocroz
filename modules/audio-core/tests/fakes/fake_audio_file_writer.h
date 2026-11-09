// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioFileWriter。書かれたサンプルをメモリに溜める。
// 書き込みの遅延（ゲートを開けるまで write() を返さない）と、容量不足（決めたフレーム数を
// 超えたら kDiskFull）を模擬できるので、ディスクの不調でもオーディオスレッドが止まらないことを
// 確かめるテストに使う。
#pragma once

#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/audio_core/ports/audio_file_writer.h"

namespace vocroz::audio_core {

/// メモリに書き出す AudioFileWriter。
/// スレッド安全性: write() と finish() はディスク I/O スレッド（テストの別スレッド）から、
/// 設定と取得はテストのスレッドから呼んでよい（内部で排他する）。
class FakeAudioFileWriter final : public AudioFileWriter {
public:
    /// @param channel_count 1 フレームのサンプル数
    explicit FakeAudioFileWriter(std::size_t channel_count) : channel_count_(channel_count) {}

    /// 書き込めるフレーム数の上限を決める。超える write() は kDiskFull を返す。
    void set_capacity_frames(std::size_t frames) {
        const std::lock_guard lock(mutex_);
        capacity_frames_ = frames;
    }

    /// 書き込みを止める。release() を呼ぶまで write() は返らない（ディスクの遅延の模擬）。
    void stall() {
        const std::lock_guard lock(mutex_);
        stalled_ = true;
    }

    /// stall() で止めた書き込みを再開する。
    void release() {
        {
            const std::lock_guard lock(mutex_);
            stalled_ = false;
        }
        released_.notify_all();
    }

    /// write() の中で止まっているなら true を返す。
    [[nodiscard]] bool is_blocked() const {
        const std::lock_guard lock(mutex_);
        return blocked_;
    }

    /// 書かれたサンプル（インターリーブ形式）を返す。
    [[nodiscard]] std::vector<float> samples() const {
        const std::lock_guard lock(mutex_);
        return samples_;
    }

    /// 書かれたフレーム数を返す。
    [[nodiscard]] std::size_t frames() const {
        const std::lock_guard lock(mutex_);
        return samples_.size() / channel_count_;
    }

    /// finish() が呼ばれたら true を返す。
    [[nodiscard]] bool finished() const {
        const std::lock_guard lock(mutex_);
        return finished_;
    }

    [[nodiscard]] common::Result<void, AudioFileError> write(
        std::span<const float> interleaved) override {
        std::unique_lock lock(mutex_);
        blocked_ = stalled_;
        released_.wait(lock, [this] { return !stalled_; });
        blocked_ = false;
        if (samples_.size() / channel_count_ + interleaved.size() / channel_count_ >
            capacity_frames_) {
            return common::make_unexpected(AudioFileError::kDiskFull);
        }
        samples_.insert(samples_.end(), interleaved.begin(), interleaved.end());
        return {};
    }

    [[nodiscard]] common::Result<void, AudioFileError> finish() override {
        const std::lock_guard lock(mutex_);
        finished_ = true;
        return {};
    }

private:
    std::size_t channel_count_;
    mutable std::mutex mutex_;
    std::condition_variable released_;
    std::vector<float> samples_;
    std::size_t capacity_frames_ = std::numeric_limits<std::size_t>::max();
    bool stalled_ = false;
    bool blocked_ = false;
    bool finished_ = false;
};

}  // namespace vocroz::audio_core
