// SPDX-License-Identifier: AGPL-3.0-only
//
// jitter_buffer.h の実装。
// スロットはタイムスタンプをフレーム長で割った位置の剰余で引く固定長のリングにし、
// 格納するフレームは常に「先頭の位置から容量ぶんの範囲」に収める。
// 範囲内では位置とスロットが 1 対 1 になるので、
// スロットに残っているフレームのタイムスタンプを比べるだけで重複と空きを判定できる。

#include "jitter_buffer.h"

#include <algorithm>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::transport {

JitterBuffer::JitterBuffer(std::size_t frame_samples, std::size_t capacity_frames)
    : frame_samples_(std::max<std::size_t>(frame_samples, 1)),
      slots_(std::max<std::size_t>(capacity_frames, 1)),
      window_samples_(static_cast<std::uint64_t>(slots_.size()) * frame_samples_) {}

InsertResult JitterBuffer::insert(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t ts = frame.timestamp;
    if (has_floor_ && !started_ && ts < floor_) {
        return InsertResult::kLate;
    }
    if (stored_ == 0 && !started_) {
        origin_ = ts;
        head_ = ts;
        store(slot_for(ts), frame);
        return InsertResult::kAccepted;
    }
    if (!aligned(ts)) {
        return InsertResult::kMisaligned;
    }
    if (started_) {
        if (ts < head_) {
            return InsertResult::kLate;
        }
        if (ts - head_ >= window_samples_) {
            // 再生位置から容量以上先のフレームは、送信側の時計の飛びか、
            // 受信側の大きな遅れを意味する。
            // どちらでも、その間にあるはずのフレームは再生する前に古くなるので、並べ直す。
            reset();
            origin_ = ts;
            head_ = ts;
            store(slot_for(ts), frame);
            return InsertResult::kDiscontinuity;
        }
    } else if (ts < head_) {
        if (newest_ - ts >= window_samples_) {
            return InsertResult::kLate;
        }
        head_ = ts;
    } else if (ts - head_ >= window_samples_) {
        // 再生前に容量を超える範囲のフレームが届いたら、新しいほうを残す。
        discard_before(ts - window_samples_ + frame_samples_);
        if (stored_ == 0) {
            head_ = ts;
        }
    }

    Slot& slot = slot_for(ts);
    if (slot.occupied && slot.frame.timestamp == ts) {
        return InsertResult::kDuplicate;
    }
    store(slot, frame);
    return InsertResult::kAccepted;
}

void JitterBuffer::start() noexcept VOCROZ_NONBLOCKING {
    if (stored_ > 0) {
        started_ = true;
        has_floor_ = false;
    }
}

PopResult JitterBuffer::pop() noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t position = head_;
    head_ += frame_samples_;
    if (stored_ == 0) {
        return {PopKind::kEmpty, nullptr};
    }
    Slot& slot = slot_for(position);
    if (!slot.occupied || slot.frame.timestamp != position) {
        return {PopKind::kMissing, nullptr};
    }
    slot.occupied = false;
    --stored_;
    return {PopKind::kFrame, &slot.frame};
}

std::size_t JitterBuffer::depth_samples() const noexcept VOCROZ_NONBLOCKING {
    if (stored_ == 0 || newest_ + frame_samples_ <= head_) {
        return 0;
    }
    return static_cast<std::size_t>(newest_ + frame_samples_ - head_);
}

std::size_t JitterBuffer::drop_until(std::size_t max_samples) noexcept VOCROZ_NONBLOCKING {
    std::size_t dropped = 0;
    while (stored_ > 0 && depth_samples() > max_samples) {
        static_cast<void>(pop());
        ++dropped;
    }
    return dropped;
}

void JitterBuffer::reset() noexcept VOCROZ_NONBLOCKING {
    for (Slot& slot : slots_) {
        slot.occupied = false;
    }
    stored_ = 0;
    started_ = false;
    has_floor_ = false;
}

void JitterBuffer::rebuffer() noexcept VOCROZ_NONBLOCKING {
    // 格納済みのフレームは再生位置より新しいので残し、最も古いものから溜め直す。
    started_ = false;
    has_floor_ = true;
    floor_ = head_;
    seek_oldest();
}

void JitterBuffer::advance_floor(std::size_t samples) noexcept VOCROZ_NONBLOCKING {
    if (has_floor_ && stored_ == 0) {
        floor_ += samples;
    }
}

JitterBuffer::Slot& JitterBuffer::slot_for(std::uint64_t timestamp) noexcept VOCROZ_NONBLOCKING {
    // 先頭より前の位置（再生前に順序が逆転して届いたフレーム）も引けるよう、
    // 符号付きの差で剰余を取る。
    const auto offset = static_cast<std::int64_t>(timestamp - origin_);
    const auto frames = offset / static_cast<std::int64_t>(frame_samples_);
    const auto count = static_cast<std::int64_t>(slots_.size());
    const auto index = ((frames % count) + count) % count;
    return slots_[static_cast<std::size_t>(index)];
}

bool JitterBuffer::aligned(std::uint64_t timestamp) const noexcept VOCROZ_NONBLOCKING {
    const auto offset = static_cast<std::int64_t>(timestamp - origin_);
    return offset % static_cast<std::int64_t>(frame_samples_) == 0;
}

void JitterBuffer::store(Slot& slot, const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING {
    slot.occupied = true;
    slot.frame = frame;
    newest_ = stored_ == 0 ? frame.timestamp : std::max(newest_, frame.timestamp);
    ++stored_;
}

void JitterBuffer::discard_before(std::uint64_t timestamp) noexcept VOCROZ_NONBLOCKING {
    if (timestamp - head_ >= window_samples_) {
        // 範囲全体より先なので、すべてのスロットが対象になる。
        for (Slot& slot : slots_) {
            slot.occupied = false;
        }
        stored_ = 0;
        head_ = timestamp;
        return;
    }
    for (std::uint64_t position = head_; position < timestamp; position += frame_samples_) {
        Slot& slot = slot_for(position);
        if (slot.occupied && slot.frame.timestamp == position) {
            slot.occupied = false;
            --stored_;
        }
    }
    head_ = timestamp;
    seek_oldest();
}

void JitterBuffer::seek_oldest() noexcept VOCROZ_NONBLOCKING {
    // 再生前の head_ は格納済みの最も古いフレームの位置なので、
    // 空きを飛ばして次のフレームまで進める。
    // 格納済みのフレームは head_ から容量の範囲にあるので、ループは容量の回数で終わる。
    while (stored_ > 0) {
        const Slot& slot = slot_for(head_);
        if (slot.occupied && slot.frame.timestamp == head_) {
            break;
        }
        head_ += frame_samples_;
    }
}

}  // namespace vocroz::transport
