// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のミキサーへ、チャンネルの追加と削除、ミュート、ゲイン、ルーティングを反映するポート。
// コンポジションルートが audio-core のミキサーへ SPSC キューかグラフのスワップで渡す。
// session はオーディオスレッドの受け渡し方を知らずに、適用すべき値だけを決める。
#pragma once

#include <string>

#include "vocroz/control_protocol/entities.h"

namespace vocroz::session {

/// 親機のミキサーの操作。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックせず、値をオーディオスレッドへ渡す。
class MixerControl {
public:
    virtual ~MixerControl() = default;

    /// 入力チャンネルを追加する。追加した直後のチャンネルはミュートで 0 dB とする（初期ミュート）。
    /// @param strip_id チャンネルストリップの entity_id
    virtual void add_channel(const std::string& strip_id) = 0;

    /// 入力チャンネルを取り除く。
    /// @param strip_id チャンネルストリップの entity_id
    virtual void remove_channel(const std::string& strip_id) = 0;

    /// チャンネルミュートを反映する。ミュート中も受信とメーターは続け、ミキサーの入力段で捨てる。
    /// @param strip_id チャンネルストリップの entity_id
    /// @param muted    ミュートするなら true
    virtual void set_channel_muted(const std::string& strip_id, bool muted) = 0;

    /// チャンネルのフェーダーを反映する。5〜20 ms のゲインランプで適用する。
    /// @param strip_id チャンネルストリップの entity_id
    /// @param gain_db  ゲイン。std::nullopt は -∞ dB
    virtual void set_channel_gain(const std::string& strip_id,
                                  control_protocol::GainDb gain_db) = 0;

    /// Master のミュートを反映する。ミュートはランプでフェードアウトし、ポップノイズを防ぐ。
    /// @param muted ミュートするなら true
    virtual void set_master_muted(bool muted) = 0;

    /// Master のフェーダーを反映する。
    /// @param gain_db ゲイン。std::nullopt は -∞ dB
    virtual void set_master_gain(control_protocol::GainDb gain_db) = 0;

    /// ルーティングの構成をミキサーのグラフにして差し替える。構成は session が検証済み
    /// （参照、つなげる組み合わせ、値の範囲、ループ）。ストリップから Master と、Master から
    /// メインの出力への結線は暗黙にあるものとして加える（docs/adr/0026-routing-staging.md）。
    /// 新しいバスとセンドはミュートで 0 dB とし（初期ミュート）、同じ entity_id のものは
    /// フェーダーとミュートの値を引き継ぐ。遅延は delay_samples() でサンプル数に換算する。
    /// @param config            適用する構成
    /// @param feedback_approved 遅延ノードを通るループをオペレーターが承認した
    /// @return 適用したら true。ミキサーが記述を受け付けなければ false で、今のグラフを使い続ける
    [[nodiscard]] virtual bool apply_routing(const control_protocol::RoutingConfig& config,
                                             bool feedback_approved) = 0;

    /// バスかセンドのミュートを反映する。
    /// @param entity_id バスかセンドの entity_id
    /// @param muted     ミュートするなら true
    virtual void set_route_muted(const std::string& entity_id, bool muted) = 0;

    /// バスかセンドのフェーダーを反映する。5〜20 ms のゲインランプで適用する。
    /// @param entity_id バスかセンドの entity_id
    /// @param gain_db   ゲイン。std::nullopt は -∞ dB
    virtual void set_route_gain(const std::string& entity_id, control_protocol::GainDb gain_db) = 0;
};

}  // namespace vocroz::session
