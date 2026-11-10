// SPDX-License-Identifier: AGPL-3.0-only
//
// PluginFault の実装。印は一方向にしか変わらないので、1 つの atomic<bool> で足りる。

#include "vocroz/audio_core/plugin_fault.h"

namespace vocroz::audio_core {

void PluginFault::raise() noexcept VOCROZ_NONBLOCKING {
    // 読む側は印を見て通知するだけで、ほかのデータを受け渡さないので relaxed で足りる。
    raised_.store(true, std::memory_order_relaxed);
}

bool PluginFault::raised() const noexcept VOCROZ_NONBLOCKING {
    return raised_.load(std::memory_order_relaxed);
}

}  // namespace vocroz::audio_core
