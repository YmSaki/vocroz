// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機への返送モニター（Minus-Me）のミックスを音声グラフに組む（docs/adr/0036-return-monitor.md）。
// 子機ごとに、その子機自身の入力を除いたミックスを作り、安全リミッターを通して返送タップへ渡す。
// 自分の声が返送に入ると、子機のスピーカーから回り込んでハウリングやエコーになるので、
// 「自分の声を含むノードを除く」判定をここ 1 か所で行う。
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 子機 1 台ぶんの返送の記述。
struct MinusMeReturn {
    /// その子機自身の入力のノード（その子機のストリップ。複数入力ならすべて）。
    /// これらと、これらから結線をたどって届くノードは、返送のミックスに入れない。
    std::vector<NodeId> own_inputs;
    /// 返送のサンプルの渡し先（null にしない）。
    std::shared_ptr<AudioSink> sink;
};

/// 返送を組んだ結果。
struct MinusMeReport {
    /// 返送ごと（returns の順）の、返送タップのノード。
    std::vector<NodeId> taps;
};

/// 返送ごとに「ミックスの候補のポストフェーダー → 安全リミッター → 返送タップ」を graph に足す。
/// 候補のうち、その子機自身の入力と、そこから結線（センドを含む）をたどって届くノードは除く。
/// 足すノードの識別子は、graph の既存のノードの最大値の次から振る。
/// @param graph 足す先のグラフの記述。ミックスの候補と子機自身の入力のノードを含むこと
/// @param mix_inputs ミックスの候補（親機のミックスに入るストリップなど）。ポストフェーダーで取る
/// @param returns 返送の記述
/// @return 足した返送タップ。候補か子機自身の入力が graph にないノードを指していれば
///         kUnknownNode、返送に AudioSink がなければ kMissingSink を返し、graph を変えない
/// スレッド安全性: 純粋関数。コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<MinusMeReport, GraphError> add_minus_me_returns(
    GraphDescription& graph, std::span<const NodeId> mix_inputs,
    std::span<const MinusMeReturn> returns);

}  // namespace vocroz::audio_core
