// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のミキサーへ、チャンネルの追加と削除、ミュート、ゲインを反映するポート。
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
};

}  // namespace vocroz::session
