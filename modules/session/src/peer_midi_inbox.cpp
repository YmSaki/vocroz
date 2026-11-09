// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerMidiInbox の実装。

#include "vocroz/session/peer_midi_inbox.h"

#include <optional>

#include "midi_timeline.h"
#include "wire/midi_message_codec.h"

namespace vocroz::session {

struct PeerMidiInbox::Strip {
    std::shared_ptr<audio_core::MidiInputQueue> queue;
    MidiTimeline timeline;
    std::optional<PeerHandle> peer;
    bool accepting = false;
};

PeerMidiInbox::PeerMidiInbox(const common::Clock& clock) : clock_(clock) {}

PeerMidiInbox::~PeerMidiInbox() = default;

std::shared_ptr<audio_core::MidiInputQueue> PeerMidiInbox::admit(
    PeerHandle peer, const std::string& strip_id, const audio_core::MidiInputQueueConfig& config) {
    const std::lock_guard lock(mutex_);
    // 別のストリップに結び付いていた接続なら、先にそちらを外す。
    if (const auto it = peers_.find(peer); it != peers_.end()) {
        unbind(*it->second);
    }
    auto& entry = strips_[strip_id];
    if (!entry) {
        entry = std::make_unique<Strip>();
        entry->queue = std::make_shared<audio_core::MidiInputQueue>(config);
    }
    if (entry->peer) {
        unbind(*entry);
    }
    entry->peer = peer;
    entry->timeline.reset();
    entry->accepting = true;
    peers_[peer] = entry.get();
    return entry->queue;
}

void PeerMidiInbox::suspend(PeerHandle peer) {
    const std::lock_guard lock(mutex_);
    if (const auto it = peers_.find(peer); it != peers_.end()) {
        it->second->accepting = false;
        it->second->queue->request_panic();
    }
}

void PeerMidiInbox::resume(PeerHandle peer) {
    const std::lock_guard lock(mutex_);
    if (const auto it = peers_.find(peer); it != peers_.end()) {
        it->second->accepting = true;
    }
}

void PeerMidiInbox::close(PeerHandle peer) {
    const std::lock_guard lock(mutex_);
    if (const auto it = peers_.find(peer); it != peers_.end()) {
        unbind(*it->second);
    }
}

void PeerMidiInbox::remove_strip(const std::string& strip_id) {
    const std::lock_guard lock(mutex_);
    if (const auto it = strips_.find(strip_id); it != strips_.end()) {
        unbind(*it->second);
        strips_.erase(it);
    }
}

std::shared_ptr<audio_core::MidiInputQueue> PeerMidiInbox::queue_for(const std::string& strip_id) {
    const std::lock_guard lock(mutex_);
    const auto it = strips_.find(strip_id);
    return it == strips_.end() ? nullptr : it->second->queue;
}

// 結び付きを外し、発音中のノートを止める。呼び出し側がロックを取っている。
void PeerMidiInbox::unbind(Strip& strip) {
    strip.queue->request_panic();
    strip.accepting = false;
    if (strip.peer) {
        peers_.erase(*strip.peer);
        strip.peer.reset();
    }
}

void PeerMidiInbox::on_midi_message(PeerHandle peer, std::span<const std::uint8_t> message) {
    // 受信時刻は、デコードやロックの待ちを含めないよう最初に読む。
    const common::Clock::TimePoint arrival = clock_.now();
    const auto events = wire::decode_midi(message);
    const std::lock_guard lock(mutex_);
    const auto it = peers_.find(peer);
    if (!events || it == peers_.end() || !it->second->accepting) {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Strip& entry = *it->second;
    for (const wire::PeerMidiEvent& event : events.value()) {
        audio_core::TimestampedMidiMessage queued;
        queued.timestamp = entry.timeline.map(event.sender_time_us, arrival);
        queued.data = event.data;
        queued.size = event.size;
        // あふれたときの扱い（止める側を落としたら Panic）はキューが持つ。
        (void)entry.queue->push(queued);
    }
}

std::uint64_t PeerMidiInbox::rejected_count() const noexcept {
    return rejected_.load(std::memory_order_relaxed);
}

}  // namespace vocroz::session
