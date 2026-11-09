// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MidiSource。テストが積んだイベントを、指定したブロックでそのまま返す。
// タイムスタンプの変換を通さずに、MIDI
// の経路（合わせ方、Panic、差し替え）だけを確かめるために使う。
#pragma once

#include <cstddef>
#include <vector>

#include "vocroz/audio_core/ports/midi_source.h"

namespace vocroz::audio_core {

/// 次の pull() で返すイベントと Panic をテストが決める MidiSource。
/// スレッド安全性: テストのスレッドだけが使う。queue() と request_panic() はグラフに渡す前か、
/// process() の合間に呼ぶ。
class FakeMidiSource final : public MidiSource {
public:
    FakeMidiSource() {
        pending_.reserve(kCapacity);
    }

    /// 次の pull() で返すイベントを積む。
    void queue(const MidiEvent& event) {
        pending_.push_back(event);
    }

    /// 次の pull() で Panic を求める。
    void request_panic() noexcept {
        panic_ = true;
    }

    /// pull() が呼ばれた回数を返す。
    [[nodiscard]] std::size_t pull_count() const noexcept {
        return pull_count_;
    }

    [[nodiscard]] MidiPullResult pull(const MidiBlockClock& /*clock*/,
                                      std::span<MidiEvent> out) noexcept
        VOCROZ_NONBLOCKING override {
        ++pull_count_;
        MidiPullResult result;
        result.panic = panic_;
        panic_ = false;
        for (; result.count < pending_.size() && result.count < out.size(); ++result.count) {
            out[result.count] = pending_[result.count];
        }
        // 書き切れなかったぶんは捨てる（テストでは容量を超えて積まない）。clear() は解放しない。
        pending_.clear();
        return result;
    }

private:
    static constexpr std::size_t kCapacity = 4096;

    std::vector<MidiEvent> pending_;
    bool panic_ = false;
    std::size_t pull_count_ = 0;
};

}  // namespace vocroz::audio_core
