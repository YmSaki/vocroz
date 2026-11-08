// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグイン遅延補正（PDC）の計算。plan_graph() が処理順を決めた後に呼ぶ。
// 規則は vocroz/audio_core/latency_compensation.h に書いた。
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/latency_compensation.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 処理順に沿って各ノードの遅延を求め、合流する結線の補正を決める。
/// @param description 検証済みのグラフの記述
/// @param order ノードの処理順（GraphPlan::order）
/// @param inputs_of ノードごとの、入る結線の位置（GraphPlan::inputs_of）
/// @param from_of 結線ごとの、送り元のノードの位置（GraphPlan::from_of）
/// @return 遅延と補正。経路の遅延が kMaxLatencyCompensationSamples を超えるなら kLatencyOutOfRange
/// スレッド安全性: 純粋関数。オーディオスレッド外から呼ぶ。
[[nodiscard]] common::Result<LatencyReport, GraphError> compensate_latency(
    const GraphDescription& description, std::span<const std::size_t> order,
    const std::vector<std::vector<std::size_t>>& inputs_of, std::span<const std::size_t> from_of);

}  // namespace vocroz::audio_core
