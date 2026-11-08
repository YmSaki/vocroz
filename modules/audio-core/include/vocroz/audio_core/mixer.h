// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のミキサー。音声グラフ、フェーダー、メーター、安全ミュート（SAFETY MUTE）をまとめ、
// 出力デバイスのコールバック（AudioRenderCallback）として音声を生成する。
//
// スレッドの分担（AGENTS.md 2.3）:
// - コントロールスレッド: グラフの適用、フェーダーの変更、メーターの読み取り、古いグラフの破棄
// - オーディオスレッド: render()
// - 任意のスレッド: engage_safety_mute()（出力デバイスの抜去を検知したスレッドから直接呼べる）
#pragma once

#include <cstddef>
#include <memory>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/meter_reading.h"
#include "vocroz/audio_core/ports/audio_render_callback.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// Mixer の構成。構築時に必要なメモリをすべて確保する。
struct MixerConfig {
    /// フェーダーの数。Alpha の 8 チャンネル + Master に、Beta1
    /// のバスを足しても足りる大きさにする。
    std::size_t fader_capacity = 64;
    /// メーターの数。fader_capacity と同じ考え方で決める。
    std::size_t meter_capacity = 64;
    /// ゲインランプの長さ（ms）。5〜20ms に丸める。
    float ramp_duration_ms = 10.0F;
};

/// Mixer の操作に失敗した原因。
enum class MixerError {
    kQueueFull,  ///< オーディオスレッドへのキューが満杯。時間をおいて再送する
    kFaderOutOfRange,  ///< フェーダーの識別子が MixerConfig::fader_capacity を超えている
};

/// 親機のミキサー。
/// すべてのフェーダーはミュートで始まり（初期ミュート）、グラフを適用するまでは無音を出力する。
/// フェーダーの変更は SPSC キューでオーディオスレッドへ渡し、ゲインランプで適用する。
/// グラフはオーディオスレッド外で構築し、アトミックなポインター交換で差し替える。
class Mixer final : public AudioRenderCallback {
public:
    /// フェーダー、メーター、キューを確保し、空のグラフ（無音）で始める。
    /// @param config 構成
    explicit Mixer(const MixerConfig& config = {});
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    Mixer(Mixer&&) = delete;
    Mixer& operator=(Mixer&&) = delete;
    /// オーディオスレッドが render() を呼ばなくなってから破棄する。
    ~Mixer() override;

    /// グラフの記述を検証し、オーディオスレッドへ渡す。次のコールバックの先頭から使われる。
    /// フェーダーとメーターの状態はグラフの外にあるので、差し替えても値とランプが続く。
    /// @param description グラフの記述
    /// @return 適用したら成功。記述が不正なら GraphError を返し、現在のグラフをそのまま使う
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] common::Result<void, GraphError> apply_graph(const GraphDescription& description);

    /// フェーダーのゲインを変える。ゲインランプで変化させる。
    /// @param fader 対象のフェーダー
    /// @param db ゲイン（dB）。-∞ 〜 +10 dB。範囲外は丸め、NaN は -∞ dB として扱う
    /// @return キューに積めたら成功
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] common::Result<void, MixerError> set_fader_gain_db(FaderId fader, float db);

    /// フェーダーのミュートを変える。ゲインランプで変化させる。
    /// ミュート中も、ストリップのプリフェーダーのメーターは入力を測り続ける。
    /// @param fader 対象のフェーダー
    /// @param muted ミュートするなら true
    /// @return キューに積めたら成功
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] common::Result<void, MixerError> set_fader_muted(FaderId fader, bool muted);

    /// フェーダーを初期状態（ミュート、0 dB）に戻し、ランプを使わずに無音にする。
    /// 新しい参加者にストリップを割り当てるときに使う（新規参加時の初期ミュート）。
    /// @param fader 対象のフェーダー
    /// @return キューに積めたら成功
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] common::Result<void, MixerError> reset_fader(FaderId fader);

    /// 安全ミュート（SAFETY
    /// MUTE）をかける。次のコールバックから、ランプを使わずに出力を無音にする。
    /// 出力デバイスの抜去やエンジンの異常を検知したときに使う。release_safety_mute() まで続く。
    /// スレッド安全性: どのスレッドからも呼び出し可能。ロックを取らず、ブロックしない。
    void engage_safety_mute() noexcept;

    /// 安全ミュートを解除する。ゲインランプで出力を戻す。
    /// オペレーターの明示的なミュート解除の操作でだけ呼ぶ（判断は session が行う）。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    void release_safety_mute() noexcept;

    /// 安全ミュートがかかっていれば true を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] bool is_safety_muted() const noexcept;

    /// メーターの値を読む。ピークは読むたびにリセットする。
    /// @param meter 対象のメーター。範囲外なら 0 を返す
    /// @return ピークと RMS
    /// スレッド安全性: コントロールスレッドから呼ぶ（読み取り側は 1 スレッドに限る）。
    [[nodiscard]] MeterReading read_meter(MeterId meter) noexcept;

    /// オーディオスレッドが使い終えた古いグラフを破棄する。
    /// コントロールスレッドの周期処理（メーターの読み取りなど）で定期的に呼ぶ。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    void collect_garbage() noexcept;

    /// 最後に適用したグラフをオーディオスレッドが使い始め、古いグラフを破棄し終えていれば true
    /// を返す。 グラフから外した AudioSource の後始末は、これが true になってから始める。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] bool is_graph_settled() noexcept;

    /// 出力するサンプルを生成する。コールバック長が kBlockSize と違っても、kBlockSize
    /// 以下に分けて処理する。 出力の 1、2 チャンネル目に Master の L、R を書き、3
    /// チャンネル目以降は無音にする。 モノラルのデバイスには L と R の平均を書く。出力は必ず -1〜1
    /// の有限の値になる。
    /// @param out 書き込み先
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    void render(InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::audio_core
