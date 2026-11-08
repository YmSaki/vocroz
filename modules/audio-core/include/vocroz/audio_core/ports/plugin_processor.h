// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフのプラグインノードが処理を呼び出すポート。
// VST3 のエフェクト（B4）と VSTi（B5）を plugin-host が JUCE
// で実装する（docs/adr/0009-beta-module-layout.md）。 グラフ自体は audio-core が持つので、JUCE の
// AudioProcessorGraph は使わず、プラグイン 1 つを グラフの 1
// ノードとしてこのポート越しに呼ぶ。プラグインノードの規則は docs/adr/0024-plugin-node-and-pdc.md。
#pragma once

#include <cstddef>
#include <span>

#include "vocroz/audio_core/midi_event.h"
#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// プラグインの入出力のチャンネル数。
struct PluginBusLayout {
    /// 入力のチャンネル数（0〜2）。0 は音声入力を持たないプラグイン（VSTi）。
    std::size_t input_channels = 2;
    /// 出力のチャンネル数（1〜2）。
    std::size_t output_channels = 2;
};

/// 1 ブロックぶんの処理の引数。
struct PluginProcessContext {
    /// 処理するバッファ（その場で書き換える）。チャンネル数は入力と出力の多い方。
    /// 先頭の input_channels 本に入力が入っていて、残りは 0 で埋めてある。
    /// 処理後は先頭の output_channels 本を出力として読む。
    std::span<float* const> channels;
    /// 処理するサンプル数（kBlockSize 以下）。
    std::size_t frames = 0;
    /// このブロックの MIDI イベント（sample_offset の昇順）。エフェクトには空の範囲を渡す。
    std::span<const MidiEvent> midi;
};

/// process() の結果。
enum class PluginProcessStatus {
    kOk,  ///< 処理した
    /// 処理できなかった（プラグインの例外、内部のエラー、クラッシュ隔離先のプロセスの停止など）。
    /// グラフはこのノードの出力を無音にし、グラフを差し替えるまで以降のブロックも無音にする。
    kFailed,
};

/// グラフの 1 ノードとして処理するプラグイン。
/// 実装は、グラフに渡す前に kSampleRate、kBlockSize、layout()
/// のチャンネル数で処理の準備を済ませておく。 グラフには std::shared_ptr
/// で渡す。古いグラフはオーディオスレッド外で破棄されるので、
/// 最後の参照もオーディオスレッド外で外れる。
class PluginProcessor {
public:
    virtual ~PluginProcessor() = default;

    /// 入出力のチャンネル数を返す。グラフの記述（NodeSpec::make_plugin()）を作るときに読む。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual PluginBusLayout layout() const noexcept = 0;

    /// プラグインが報告する処理の遅延（サンプル、kSampleRate での値）を返す。
    /// プラグイン遅延補正（PDC）は、グラフの記述を作った時点のこの値で計算する。値が変わったら、
    /// 呼び出し側（session）がグラフを記述し直して適用する。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual std::size_t latency_samples() const noexcept = 0;

    /// 1 ブロックを処理する。
    /// @param context 処理するバッファ、サンプル数、MIDI イベント
    /// @return 処理の結果。kFailed なら、グラフはこのノードの出力を無音にする
    /// エラー時の振る舞い: 例外を送出しない。実装はプラグインの例外を捕まえて kFailed を返す。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    [[nodiscard]] virtual PluginProcessStatus process(const PluginProcessContext& context) noexcept
        VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
