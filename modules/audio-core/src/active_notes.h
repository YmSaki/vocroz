// SPDX-License-Identifier: AGPL-3.0-only
//
// 発音中のノートとサステインペダルの追跡。
// MIDI の経路（ソース → プラグイン）ごとに 1 つ持ち、その経路で送った Note On のうち、まだ Note Off
// を送っていないものを覚える。経路の切断、子機の LOST、キューのあふれでは、覚えているノートすべてに
// Note Off を出して音残り（Stuck Note）を防ぐ（All Notes Off、Panic）。
// コントロールチェンジ 123（All Notes Off）を送るだけにしないのは、VST3 では MIDI の CC が
// プラグインのパラメーターに写されるだけで、All Notes Off を解釈しないプラグインがあるため。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/audio_core/midi_event.h"
#include "vocroz/audio_core/midi_message.h"
#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// release_all() が一度に書く MIDI イベントの最大数。
/// 全チャンネル・全ノートの Note Off と、全チャンネルのサステインのオフ。
inline constexpr std::size_t kMaxReleaseEvents = kMidiChannels * kMidiNotes + kMidiChannels;

/// 発音中のノートの集合。トリビアルにコピーできるので、グラフの差し替えで状態を引き継げる。
/// スレッド安全性: 1 つのスレッド（オーディオスレッド）だけが使う。
class ActiveNotes {
public:
    /// 送るイベントを見て、発音中のノートとサステインの状態を更新する。
    /// @param event 送るイベント
    void observe(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING;

    /// 発音中のノートとサステインがあれば true を返す。
    [[nodiscard]] bool any() const noexcept VOCROZ_NONBLOCKING;

    /// 発音中のノートすべての Note Off と、踏まれているサステインのオフを out
    /// に書き、状態を空にする。
    /// @param out 書き込み先（kMaxReleaseEvents 以上の長さにすると必ず書き切れる）
    /// @param sample_offset 書くイベントのサンプル位置
    /// @return 書いたイベント数。out が足りなければ書けたぶんだけ書き、書けなかったノートは残す
    [[nodiscard]] std::size_t release_all(std::span<MidiEvent> out,
                                          std::uint32_t sample_offset) noexcept VOCROZ_NONBLOCKING;

    /// other の発音中のノートとサステインを、この集合に加える。
    /// @param other 加える集合
    void merge(const ActiveNotes& other) noexcept VOCROZ_NONBLOCKING;

private:
    // チャンネルごとの、ノート番号のビット集合（128 ビット）。
    using NoteBits = std::array<std::uint64_t, kMidiNotes / 64>;

    std::array<NoteBits, kMidiChannels> notes_{};
    std::array<bool, kMidiChannels> sustain_{};
};

}  // namespace vocroz::audio_core
