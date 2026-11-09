// SPDX-License-Identifier: AGPL-3.0-only
//
// RecordingTrack の実装。オーディオスレッド側（push）はリングバッファと欠落のキューに書くだけで、
// ディスク I/O スレッド側（drain）がそれを読んでファイルに書き出す。

#include "vocroz/audio_core/recording_track.h"

#include <algorithm>
#include <atomic>
#include <span>
#include <vector>

#include "vocroz/common/spsc_queue.h"

#include "audio_core_constants.h"
#include "sample_ring_buffer.h"

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// 録音の欠落 1 件。リングバッファの通算位置 position の直前に frames フレームの無音が入る。
struct GapMark {
    std::uint64_t position = 0;
    std::uint64_t frames = 0;
};

// failure の値。0 は失敗なし、それ以外は AudioFileError の値 + 1。
constexpr int kNoFailure = 0;

}  // namespace

struct RecordingTrack::State {
    State(std::size_t channel_count, std::size_t buffer_frames)
        : ring(channel_count, buffer_frames),
          scratch(kDiskChunkFrames * ring.channel_count(), 0.0F) {}

    SampleRingBuffer ring;
    common::SpscQueue<GapMark, kRecordingGapQueueCapacity> gaps;
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<int> failure{kNoFailure};

    // まだキューに入れていない欠落のフレーム数。録音中はオーディオスレッドだけが読み書きし、
    // finish()（オーディオスレッドが push() を呼ばなくなった後）だけがディスク I/O
    // スレッドから読む。
    // 受け渡しの順序はグラフの破棄で保たれるが、読み書きをアトミックにして前提を明示する。
    std::atomic<std::uint64_t> pending_gap{0};

    // ディスク I/O スレッドだけが読み書きする。
    std::optional<GapMark> next_gap;  // キューから取り出し、まだ無音を書いていない欠落
    std::vector<float> scratch;  // ファイルへ渡すインターリーブ形式の作業領域

    // 書き出しの失敗を記録する。以降の push() は何もせず、drain() は同じエラーを返す。
    void fail(AudioFileError error) noexcept {
        failure.store(static_cast<int>(error) + 1, std::memory_order_relaxed);
    }

    // scratch の先頭 frames フレームを書き出す。
    common::Result<void, AudioFileError> write_scratch(AudioFileWriter& writer,
                                                       std::size_t frames) {
        auto result =
            writer.write(std::span<const float>(scratch.data(), frames * ring.channel_count()));
        if (!result) {
            fail(result.error());
        }
        return result;
    }

    // frames フレームの無音を、作業領域の大きさずつ書き出す。
    common::Result<void, AudioFileError> write_silence(AudioFileWriter& writer,
                                                       std::size_t frames) {
        std::fill(scratch.begin(), scratch.end(), 0.0F);
        while (frames > 0) {
            const std::size_t chunk = std::min(frames, kDiskChunkFrames);
            if (auto result = write_scratch(writer, chunk); !result) {
                return result;
            }
            frames -= chunk;
        }
        return {};
    }
};

RecordingTrack::RecordingTrack(std::size_t channel_count, std::size_t buffer_frames)
    : state_(std::make_unique<State>(channel_count, buffer_frames)) {}

RecordingTrack::~RecordingTrack() = default;

std::size_t RecordingTrack::channel_count() const noexcept {
    return state_->ring.channel_count();
}

void RecordingTrack::push(const float* const* channels,
                          std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    State& state = *state_;
    if (state.failure.load(std::memory_order_relaxed) != kNoFailure) {
        return;
    }
    // 欠落の後のサンプルは、欠落の位置をディスク I/O スレッドへ渡せてから書く。
    // 渡せないまま書くと、無音を入れる位置がわからなくなり、以降の時間軸がずれるため。
    std::uint64_t pending = state.pending_gap.load(std::memory_order_relaxed);
    if (pending > 0) {
        const GapMark gap{state.ring.write_position(), pending};
        if (frames <= state.ring.writable_frames() && state.gaps.try_push(gap)) {
            pending = 0;
        }
    }
    if (pending > 0 || !state.ring.write_planar(channels, frames)) {
        pending += frames;
        state.dropped.fetch_add(frames, std::memory_order_relaxed);
    }
    state.pending_gap.store(pending, std::memory_order_relaxed);
}

common::Result<std::size_t, AudioFileError> RecordingTrack::drain(AudioFileWriter& writer) {
    State& state = *state_;
    if (const auto failure = this->failure()) {
        // 失敗後に届いたサンプルは書かずに捨て、溜め続けないようにする。
        state.ring.skip_to(state.ring.write_position());
        while (state.gaps.try_pop()) {
        }
        return make_unexpected(*failure);
    }
    std::size_t written = 0;
    while (true) {
        if (!state.next_gap) {
            state.next_gap = state.gaps.try_pop();
        }
        const std::uint64_t read = state.ring.read_position();
        if (state.next_gap && state.next_gap->position == read) {
            // 欠落した区間を無音で埋め、後続のサンプルの時間軸を保つ。
            const auto frames = static_cast<std::size_t>(state.next_gap->frames);
            state.next_gap.reset();
            if (auto result = state.write_silence(writer, frames); !result) {
                return make_unexpected(result.error());
            }
            written += frames;
            continue;
        }
        std::size_t limit = kDiskChunkFrames;
        if (state.next_gap) {
            limit = static_cast<std::size_t>(
                std::min<std::uint64_t>(limit, state.next_gap->position - read));
        }
        const std::size_t frames = state.ring.read_interleaved(state.scratch, limit);
        if (frames == 0) {
            break;
        }
        if (auto result = state.write_scratch(writer, frames); !result) {
            return make_unexpected(result.error());
        }
        written += frames;
    }
    return written;
}

common::Result<std::size_t, AudioFileError> RecordingTrack::finish(AudioFileWriter& writer) {
    State& state = *state_;
    auto drained = drain(writer);
    if (!drained) {
        // 容量不足などで止まっても、書けたところまでを再生できるファイルにして残す。
        (void)writer.finish();
        return drained;
    }
    // 録音の最後まで欠落が続いていたら、後に続くサンプルがないのでキューに入っていない。
    // ここで無音を書き、ファイルの長さを他のトラックとそろえる。
    const auto trailing =
        static_cast<std::size_t>(state.pending_gap.exchange(0, std::memory_order_relaxed));
    if (auto result = state.write_silence(writer, trailing); !result) {
        (void)writer.finish();
        return make_unexpected(result.error());
    }
    if (auto result = writer.finish(); !result) {
        state.fail(result.error());
        return make_unexpected(result.error());
    }
    return drained.value() + trailing;
}

std::uint64_t RecordingTrack::dropped_frames() const noexcept {
    return state_->dropped.load(std::memory_order_relaxed);
}

std::optional<AudioFileError> RecordingTrack::failure() const noexcept {
    const int failure = state_->failure.load(std::memory_order_relaxed);
    if (failure == kNoFailure) {
        return std::nullopt;
    }
    return static_cast<AudioFileError>(failure - 1);
}

}  // namespace vocroz::audio_core
