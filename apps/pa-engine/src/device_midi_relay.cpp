// SPDX-License-Identifier: AGPL-3.0-only
//
// device_midi_relay.h の実装。

#include "device_midi_relay.h"

#include <utility>

#include "mixer_graph_builder.h"

namespace vocroz::engine {

void DeviceMidiRelay::on_midi_device_opened(const std::string& device_id,
                                            std::shared_ptr<audio_core::MidiInputQueue> queue) {
    loop_.post([this, key = device_midi_key(device_id), queue = std::move(queue)]() mutable {
        // つなげなかったら EngineMixer が記録する。キューは誰も取り出さないので、あふれたぶんは
        // 捨てられるだけで、音には影響しない。
        static_cast<void>(mixer_.attach_midi_source(key, std::move(queue)));
    });
}

void DeviceMidiRelay::on_midi_device_closed(const std::string& device_id) {
    // Panic はキューに求め済み。グラフから外すと、発音中のノートには外した後の最初のブロックで
    // Note Off が送られる。
    loop_.post([this, key = device_midi_key(device_id)] { mixer_.detach_midi_source(key); });
}

}  // namespace vocroz::engine
