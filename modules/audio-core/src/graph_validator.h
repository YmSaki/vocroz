// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフの記述の検証と、処理順の決定。
// RenderGraph::build()（構築）と Mixer::validate_graph()（ステージング中の検証）が同じ規則を
// 使うよう、検証をバッファの確保から分けて 1 か所に置く。
#pragma once

#include <cstddef>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/latency_compensation.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 検証済みのグラフの処理計画。添字は GraphDescription::nodes と connections の位置。
struct GraphPlan {
    /// ノードの処理順。遅延ノードへ入る結線は同じブロックの依存として扱わない。
    std::vector<std::size_t> order;
    /// ノードごとの、そのノードへ入る結線の位置。
    std::vector<std::vector<std::size_t>> inputs_of;
    /// 結線ごとの、送り元のノードの位置。
    std::vector<std::size_t> from_of;
    /// ノードごとの、プリフェーダーのセンドの送り元になっているか。
    std::vector<bool> has_pre_fader_send;
    /// ノードごとの、そのノードへ MIDI を送る MIDI ソースの位置（GraphDescription::midi_sources
    /// の添字、 midi_connections に書かれた順）。プラグインノード以外は空。
    std::vector<std::vector<std::size_t>> midi_sources_of;
    /// プラグイン遅延補正（latency_compensation.h）。
    LatencyReport latency;
};

/// ノードの出力チャンネル数を返す。モノラルの入力（ソース）とストリップは 1、
/// バスから後段はステレオ。プラグインは PluginBusLayout::output_channels。
/// @param spec ノードの記述
[[nodiscard]] std::size_t output_channels_of(const NodeSpec& spec) noexcept;

/// ノードが入力の加算で受け取るチャンネル数を返す。プラグインは PluginBusLayout::input_channels
/// （0 なら音声入力を受け付けない）、それ以外は出力チャンネル数と同じ。
/// @param spec ノードの記述
[[nodiscard]] std::size_t input_channels_of(const NodeSpec& spec) noexcept;

/// 記述を検証し、処理計画を作る。
/// @param description グラフの記述
/// @param fader_capacity 使ってよい FaderId の上限（この値未満）
/// @param meter_capacity 使ってよい MeterId の上限（この値未満）
/// @return 処理計画。記述が不正なら GraphError
/// スレッド安全性: 純粋関数。オーディオスレッド外から呼ぶ。
[[nodiscard]] common::Result<GraphPlan, GraphError> plan_graph(const GraphDescription& description,
                                                               std::size_t fader_capacity,
                                                               std::size_t meter_capacity);

}  // namespace vocroz::audio_core
