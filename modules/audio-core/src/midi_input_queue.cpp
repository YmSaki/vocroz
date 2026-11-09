// SPDX-License-Identifier: AGPL-3.0-only
//
// MidiInputQueue の実装。

#include "vocroz/audio_core/midi_input_queue.h"

#include <algorithm>

#include "vocroz/audio_core/midi_message.h"

#include "midi_timing.h"

namespace vocroz::audio_core {
namespace {

std::span<const std::uint8_t> bytes_of(const TimestampedMidiMessage& message) noexcept
    VOCROZ_NONBLOCKING {
    return std::span<const std::uint8_t>(message.data.data(),
                                         std::min<std::size_t>(message.size, message.data.size()));
}

}  // namespace

MidiInputQueue::MidiInputQueue(const MidiInputQueueConfig& config) noexcept
    : schedule_ahead_samples_(config.schedule_ahead_samples) {}

bool MidiInputQueue::push(const TimestampedMidiMessage& message) noexcept VOCROZ_NONBLOCKING {
    if (!is_channel_message(bytes_of(message))) {
        return false;
    }
    if (queue_.try_push(message)) {
        return true;
    }
    dropped_.fetch_add(1, std::memory_order_relaxed);
    if (is_release_message(make_midi_event(bytes_of(message)))) {
        // フェイルセーフ: 止める側のメッセージを落とすと、そのノートは鳴り続ける。
        // 全部止めるほうが、演奏が一瞬途切れるより害が小さい（AGENTS.md 1.2）。
        panic_requested_.store(true, std::memory_order_release);
    }
    return false;
}

void MidiInputQueue::request_panic() noexcept {
    panic_requested_.store(true, std::memory_order_release);
}

MidiPullResult MidiInputQueue::pull(const MidiBlockClock& clock,
                                    std::span<MidiEvent> out) noexcept VOCROZ_NONBLOCKING {
    MidiPullResult result;
    if (panic_requested_.exchange(false, std::memory_order_acq_rel)) {
        // Panic より前に積まれたメッセージは捨てる。残すと、Note Off の後に古い Note On が届いて
        // 音が戻る。捨てる数をキューの容量で打ち切り、生産者が積み続けても終わるようにする。
        held_.reset();
        for (std::size_t i = 0; i < kMidiQueueCapacity && queue_.try_pop(); ++i) {
        }
        result.panic = true;
    }
    std::uint32_t last_offset = 0;
    while (result.count < out.size()) {
        std::optional<TimestampedMidiMessage> message = held_;
        held_.reset();
        if (!message) {
            message = queue_.try_pop();
        }
        if (!message) {
            break;
        }
        const auto offset =
            sample_offset_in_block(clock, message->timestamp, schedule_ahead_samples_);
        if (!offset) {
            // 後続も同じかより後に鳴らすので、ここで止めて届いた順を保つ。
            held_ = message;
            break;
        }
        // タイムスタンプが前後していても、届いた順を優先する。入れ替えると、同じノートの
        // Note Off が Note On より先に処理され、音が止まらなくなることがあるため。
        last_offset = std::max(*offset, last_offset);
        out[result.count++] = make_midi_event(bytes_of(*message), last_offset);
    }
    return result;
}

std::uint64_t MidiInputQueue::dropped_count() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
}

}  // namespace vocroz::audio_core
