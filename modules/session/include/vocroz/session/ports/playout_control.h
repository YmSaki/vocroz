// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の受信側（子機ごとの再生ストリーム）へ、全体の目標レイテンシーを反映するポート。
// コンポジションルートが受信中の各ストリームの PlayoutStream::set_max_delay() へ配り、
// あとから受信を始めたストリームも同じ目標で作る。
// session はジッターバッファの作りを知らずに、オペレーターが選んだ目標だけを決める。
#pragma once

#include <chrono>

namespace vocroz::session {

/// 受信側の再生の操作。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックせず、値をオーディオスレッドへ渡す。
class PlayoutControl {
public:
    virtual ~PlayoutControl() = default;

    /// 全チャンネルのジッターバッファの深さの上限（目標レイテンシー）を変える。
    /// 値の範囲は session が検証済み（10〜250ms）。
    /// @param target 新しい上限
    virtual void set_latency_target(std::chrono::microseconds target) = 0;
};

}  // namespace vocroz::session
