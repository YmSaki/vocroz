// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフが MIDI イベントを取り出す先のポート。
// 親機では MidiInputQueue（midi_input_queue.h）が実装し、MIDI デバイスの入力（device）と子機からの
// MIDI（transport の信頼制御層の MIDI レーン）を受けてオーディオスレッドへ渡す。
// AudioSource と同じく、オーディオスレッドから引き出す（pull）形にする。オーディオスレッドが
// 他のスレッドを待たず、ブロックごとに必要なぶんだけ取り出すため。
// MIDI の経路は PCM の結線とは独立に、GraphDescription::midi_connections で決める
// （docs/adr/0029-midi-and-vsti.md）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/audio_core/midi_event.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/ports/clock.h"

namespace vocroz::audio_core {

/// 処理するブロックの、サンプルの時計と実時間の対応。
/// Mixer がデバイスのコールバックの先頭で時刻を読み、そのコールバックを kBlockSize 以下に分けた
/// 各ブロックに渡す。MIDI のタイムスタンプ（実時間）をブロック内のサンプル位置に変換するのに使う。
struct MidiBlockClock {
    /// このブロックの先頭のサンプル位置（Mixer が出力した累計サンプル数）。
    std::uint64_t first_sample = 0;
    /// このブロックのサンプル数（kBlockSize 以下）。
    std::size_t frames = 0;
    /// anchor_time と anchor_sample が有効なら true。時計を持たない Mixer では false で、
    /// そのときはすべてのイベントをブロックの先頭に置く。
    bool has_anchor = false;
    /// デバイスのコールバックの先頭で読んだ時刻。
    common::Clock::TimePoint anchor_time;
    /// anchor_time に対応するサンプル位置（そのコールバックの先頭のサンプル位置）。
    std::uint64_t anchor_sample = 0;
};

/// MidiSource::pull() の結果。
struct MidiPullResult {
    /// out に書いたイベント数。
    std::size_t count = 0;
    /// Panic（All Notes Off）を求めるなら
    /// true。グラフは、このソースの経路で発音中のノートすべてに、 このブロックの先頭で Note Off
    /// を送ってから、out のイベントを送る。
    bool panic = false;
};

/// MIDI イベントの取り出し先。
/// グラフ（GraphDescription）には std::shared_ptr で渡す。古いグラフはオーディオスレッド外で
/// 破棄されるので、最後の参照もオーディオスレッド外で外れる。
/// 1 つのソースを複数のプラグインへつないでも、グラフは 1 ブロックに 1 回だけ pull() する。
class MidiSource {
public:
    virtual ~MidiSource() = default;

    /// このブロックで送るイベントを out に書く。
    /// @param clock このブロックのサンプル位置と、実時間との対応
    /// @param out 書き込み先。イベントは sample_offset の昇順（同じ位置は届いた順）で書き、
    ///            sample_offset は clock.frames
    ///            未満にする。書き切れないイベントは次のブロックへ回す
    /// @return 書いたイベント数と、Panic を求めるか
    /// エラー時の振る舞い: 例外を送出しない。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    [[nodiscard]] virtual MidiPullResult pull(
        const MidiBlockClock& clock, std::span<MidiEvent> out) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
