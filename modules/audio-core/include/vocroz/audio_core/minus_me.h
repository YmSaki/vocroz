// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機への返送モニター（Minus-Me）のミックスを音声グラフに組む（docs/adr/0036-return-monitor.md）。
// 子機ごとに、その子機自身の入力を除いたミックスを作り、安全リミッターを通して返送タップへ渡す。
// 自分の声が返送に入ると、子機のスピーカーから回り込んでハウリングやエコーになるので、
// 「自分の声を含むノードを除く」判定をここ 1 か所で行う。
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 返送のミックスの候補 1 つ。親機のミックス（Master）へ入る結線の、送り元と取り出し方。
/// 返送は、この取り出し方とセンドレベルをそのまま使い、親機のミックスと同じバランスにする。
struct MinusMeInput {
    MinusMeInput() = default;

    /// ポストフェーダーで、センドレベルなしで取る候補（ストリップから Master への暗黙の結線など）。
    /// @param source 送り元
    MinusMeInput(NodeId source) noexcept  // NOLINT(google-explicit-constructor): 候補の多くは
                                          // ノードだけで決まるので、NodeId の並びで書けるようにする
        : node(source) {}

    /// @param source 送り元
    /// @param send_tap 取り出す位置
    /// @param send_level Master へ入るセンドのレベル。返送は同じフェーダーを共有して掛ける
    MinusMeInput(NodeId source, SendTap send_tap, std::optional<FaderId> send_level) noexcept
        : node(source), tap(send_tap), level(send_level) {}

    NodeId node;                        ///< 送り元
    SendTap tap = SendTap::kPostFader;  ///< 取り出す位置
    /// Master へ入るセンドのレベル。返送の結線は、このフェーダーを共有して
    /// （Connection::shares_level）ミュートと音量を親機のミックスと同じにする。
    std::optional<FaderId> level;
};

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

/// 返送ごとに「ミックスの候補 → 安全リミッター → 返送タップ」を graph に足す。候補からの結線は、
/// 候補の取り出し位置とセンドレベル（共有）を使う。候補のうち、その子機自身の入力と、
/// そこから結線（センドを含む）をたどって届くノードは除く。
/// 足すノードの識別子は、graph の既存のノードの最大値の次から振る。
/// @param graph 足す先のグラフの記述。ミックスの候補と子機自身の入力のノードを含むこと
/// @param mix_inputs ミックスの候補（親機のミックスに入るストリップや、Master へ送るバスなど）。
///                   同じ送り元が複数あれば、最初の 1 つだけを使う
/// @param returns 返送の記述
/// @return 足した返送タップ。候補か子機自身の入力が graph にないノードを指していれば
///         kUnknownNode、返送に AudioSink がなければ kMissingSink を返し、graph を変えない
/// スレッド安全性: 純粋関数。コントロールスレッドから呼ぶ。
[[nodiscard]] common::Result<MinusMeReport, GraphError> add_minus_me_returns(
    GraphDescription& graph, std::span<const MinusMeInput> mix_inputs,
    std::span<const MinusMeReturn> returns);

}  // namespace vocroz::audio_core
