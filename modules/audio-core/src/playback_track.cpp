// SPDX-License-Identifier: AGPL-3.0-only
//
// PlaybackTrack の実装。ディスク I/O スレッド側（fill）がリングバッファに先読みし、オーディオ
// スレッド側（render）がそれを読む。シークは「新しい区間がリングバッファのどこから始まるか」を
// キューでオーディオスレッドへ渡し、オーディオスレッドがフェードアウトし終えてから古い先読みを
// 読み飛ばす。リングバッファの読み込み位置を動かすのは、消費者であるオーディオスレッドだけにする。

#include "vocroz/audio_core/playback_track.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <span>
#include <vector>

#include "vocroz/audio_core/gain_ramp.h"
#include "vocroz/common/spsc_queue.h"

#include "audio_core_constants.h"
#include "sample_ring_buffer.h"

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// 「値なし」を表す位置。通算のフレーム数がこの値に届くことはない（48kHz で数百万年）。
constexpr std::uint64_t kNoPosition = std::numeric_limits<std::uint64_t>::max();

// シーク後の区間の始まり。リングバッファの通算位置 ring_start のサンプルが、ファイルの
// file_start フレーム目にあたる。
struct Segment {
    std::uint64_t ring_start = 0;
    std::uint64_t file_start = 0;
};

}  // namespace

struct PlaybackTrack::State {
    State(std::size_t channel_count, std::size_t buffer_frames)
        : ring(channel_count, buffer_frames),
          scratch(kDiskChunkFrames * ring.channel_count(), 0.0F) {}

    SampleRingBuffer ring;
    common::SpscQueue<Segment, kPlaybackSegmentQueueCapacity> segments;  // ディスク → オーディオ
    std::atomic<bool> playing{false};
    std::atomic<std::uint64_t> seek_request{kNoPosition};  // コントロール → ディスク
    std::atomic<std::uint64_t> end_position{kNoPosition};  // ファイルの終わりのリングバッファの位置
    std::atomic<std::uint64_t> position{0};  // オーディオスレッドだけが書く
    std::atomic<std::uint64_t> underruns{0};

    // オーディオスレッドだけが読み書きする。
    GainRamp fade{kPlaybackFadeSamples};
    std::optional<Segment> pending_segment;  // フェードアウトし終えたら読み飛ばす区間

    // ディスク I/O スレッドだけが読み書きする。
    std::optional<std::uint64_t> pending_seek;  // まだオーディオスレッドへ渡せていないシーク
    bool reached_end = false;
    std::vector<float> scratch;  // デコードしたインターリーブ形式のサンプルの作業領域

    // オーディオスレッドが読んだ位置がファイルの終わりに達していれば true。
    [[nodiscard]] bool at_end() const noexcept VOCROZ_NONBLOCKING {
        const std::uint64_t end = end_position.load(std::memory_order_acquire);
        return end != kNoPosition && ring.read_position() >= end;
    }
};

PlaybackTrack::PlaybackTrack(std::size_t channel_count, std::size_t buffer_frames)
    : state_(std::make_unique<State>(channel_count, buffer_frames)) {}

PlaybackTrack::~PlaybackTrack() = default;

std::size_t PlaybackTrack::channel_count() const noexcept {
    return state_->ring.channel_count();
}

void PlaybackTrack::play() noexcept {
    state_->playing.store(true, std::memory_order_relaxed);
}

void PlaybackTrack::pause() noexcept {
    state_->playing.store(false, std::memory_order_relaxed);
}

void PlaybackTrack::seek(std::uint64_t frame) noexcept {
    // 値なしの印と重ならないよう、届くことのない位置は 1 つ手前に丸める。
    state_->seek_request.store(std::min(frame, kNoPosition - 1), std::memory_order_relaxed);
}

common::Result<std::size_t, AudioFileError> PlaybackTrack::fill(AudioFileReader& reader) {
    State& state = *state_;
    if (const std::uint64_t request =
            state.seek_request.exchange(kNoPosition, std::memory_order_relaxed);
        request != kNoPosition) {
        state.pending_seek = request;
    }
    if (state.pending_seek) {
        if (auto result = reader.seek(*state.pending_seek); !result) {
            return make_unexpected(result.error());
        }
        // 古い区間のファイルの終わりは、新しい区間では意味を持たない。区間を渡す前に消す。
        state.end_position.store(kNoPosition, std::memory_order_release);
        // 区間をオーディオスレッドへ渡せるまでは、新しいサンプルを書かない。書くと、どこから
        // 新しい区間かがわからなくなるため。渡せなければ次の fill() でやり直す。
        if (!state.segments.try_push({state.ring.write_position(), *state.pending_seek})) {
            return std::size_t{0};
        }
        state.pending_seek.reset();
        state.reached_end = false;
    }
    const std::size_t channels = state.ring.channel_count();
    std::size_t total = 0;
    while (!state.reached_end) {
        const std::size_t space = std::min(state.ring.writable_frames(), kDiskChunkFrames);
        if (space == 0) {
            break;
        }
        auto read = reader.read(std::span<float>(state.scratch.data(), space * channels));
        if (!read) {
            return make_unexpected(read.error());
        }
        const std::size_t frames = std::min(read.value(), space);
        if (frames == 0) {
            state.reached_end = true;
            state.end_position.store(state.ring.write_position(), std::memory_order_release);
            break;
        }
        total += state.ring.write_interleaved(
            std::span<const float>(state.scratch.data(), frames * channels));
    }
    return total;
}

void PlaybackTrack::render(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    State& state = *state_;
    const std::size_t channel_count = state.ring.channel_count();
    while (const auto segment = state.segments.try_pop()) {
        state.pending_segment = *segment;
    }
    const bool silent = state.fade.is_settled() && state.fade.current() == 0.0F;
    if (state.pending_segment && silent) {
        // フェードアウトし終えたので、古い先読みを捨てて新しい区間へ移る。
        state.ring.skip_to(state.pending_segment->ring_start);
        state.position.store(state.pending_segment->file_start, std::memory_order_relaxed);
        state.pending_segment.reset();
    }
    const bool audible =
        state.playing.load(std::memory_order_relaxed) && !state.pending_segment.has_value();
    // ランプの途中で同じ目標を設定し直すと、ランプが最初からやり直しになるので、変わるときだけ設定する。
    if (const float target = audible ? 1.0F : 0.0F; state.fade.target() != target) {
        state.fade.set_target(target);
    }
    if (!audible && state.fade.is_settled() && state.fade.current() == 0.0F) {
        // 停止中は先読みを減らさず、位置もそのままにする。
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            std::fill(channels[ch], channels[ch] + frames, 0.0F);
        }
        return;
    }
    const std::size_t read = state.ring.read_planar(channels, frames);
    if (read < frames) {
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            std::fill(channels[ch] + read, channels[ch] + frames, 0.0F);
        }
        // ファイルの終わりの後の無音と、止める途中のフェードアウトは、先読みの遅れとして数えない。
        if (audible && !state.at_end()) {
            state.underruns.fetch_add(frames - read, std::memory_order_relaxed);
        }
    }
    state.position.store(state.position.load(std::memory_order_relaxed) + read,
                         std::memory_order_relaxed);
    state.fade.apply(channels, channel_count, frames);
}

bool PlaybackTrack::is_playing() const noexcept {
    return state_->playing.load(std::memory_order_relaxed);
}

bool PlaybackTrack::at_end() const noexcept {
    return state_->at_end();
}

std::uint64_t PlaybackTrack::position_frames() const noexcept {
    return state_->position.load(std::memory_order_relaxed);
}

std::uint64_t PlaybackTrack::underrun_frames() const noexcept {
    return state_->underruns.load(std::memory_order_relaxed);
}

}  // namespace vocroz::audio_core
